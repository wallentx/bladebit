#include "plotting/GlobalPlotConfig.h"
#include "util/CliParser.h"
#include "plotting/PlotTools.h"
#include "Version.h"

#if PLATFORM_IS_UNIX
    #include <sys/resource.h>
#endif

// CUDA-only build includes
#include "../cuda/CudaPlotter.h"

static void ParseCommandLine( GlobalPlotConfig& cfg, IPlotter*& outPlotter, int argc, const char* argv[] );
static void PrintUsage();

// MemTester.cpp (keep minimal tools)
void MemTestMain( GlobalPlotConfig& gCfg, CliParser& cli );
void MemTestPrintUsage();

// PlotChecker.cpp (minimal plotting tools)
void PlotValidatorMain( GlobalPlotConfig& gCfg, CliParser& cli );
void PlotValidatorPrintUsage();

//-----------------------------------------------------------
int main( int argc, const char* argv[] )
{
    // Install a crash handler to dump our stack traces
    SysHost::InstallCrashHandler();

#if _DEBUG
    Log::Line( "*** Warning: Debug mode is ENABLED ***" );
#endif

    IPlotter* plotter = nullptr;

    auto& cfg = *new GlobalPlotConfig{};
    ParseCommandLine( cfg, plotter, --argc, ++argv );

    const int64 plotCount = cfg.plotCount > 0 ? (int64)cfg.plotCount : std::numeric_limits<int64>::max();

    byte   plotId  [BB_PLOT_ID_LEN];
    byte   plotMemo[BB_PLOT_MEMO_MAX_SIZE];
    uint16 plotMemoSize = 0;

    char   plotIdStr[BB_PLOT_ID_LEN*2+1];

    // Prepare the output path
    char*  plotOutPath      = nullptr;
    uint32 plotOutPathIndex = 0;
    {
        // Get the largest buffer needed
        size_t outFolderLengthMax = cfg.outputFolders[0].length();

        for( uint32 i = 1; i < cfg.outputFolderCount; i++ )
            outFolderLengthMax = std::max( outFolderLengthMax, cfg.outputFolders[i].length() );

        plotOutPath = new char[outFolderLengthMax + BB_COMPRESSED_PLOT_FILE_LEN_TMP + 2]; // + '/' + null terminator
    }

    // Start plotting
    for( int64 i = 0; i < plotCount; i++ )
    {
        // Generate a plot id and memo
        PlotTools::GeneratePlotIdAndMemo( plotId, plotMemo, plotMemoSize,
                                          *cfg.farmerPublicKey, cfg.poolPublicKey, cfg.poolContractPuzzleHash );

        // Apply debug plot id and/or memo
        if( cfg.plotIdStr )
            HexStrToBytes( cfg.plotIdStr, BB_PLOT_ID_LEN*2, plotId, BB_PLOT_ID_LEN );

        if( cfg.plotMemoStr )
        {
            const size_t memoLen = strlen( cfg.plotMemoStr );
            HexStrToBytes( cfg.plotMemoStr, memoLen, plotMemo, memoLen/2 );
        }

        // Convert plot id to string
        PlotTools::PlotIdToString( plotId, plotIdStr );

        // Set the plot file name & get the full path to it
        const char* plotFileName  = nullptr;
        const char* plotOutFolder = nullptr;
        {
            // Select the next output folder
            const std::string& curOutputDir = cfg.outputFolders[plotOutPathIndex++];
            plotOutPathIndex %= cfg.outputFolderCount;

            plotOutFolder = curOutputDir.data();

            memcpy( plotOutPath, curOutputDir.data(), curOutputDir.length() );

            plotFileName = plotOutPath + curOutputDir.length();
            PlotTools::GenPlotFileName( plotId, (char*)plotFileName, cfg.compressionLevel );
        }

        // Begin plot
        if( cfg.plotCount == 0 )
            Log::Line( "Generating plot %lld: %s", i+1, plotIdStr );
        else
            Log::Line( "Generating plot %lld / %u: %s", i+1, cfg.plotCount, plotIdStr );

        Log::Line( "Plot temporary file: %s", plotOutPath );

        if( cfg.showMemo )
        {
            char plotMemoStr[BB_PLOT_MEMO_MAX_SIZE*2+1];

            size_t numEncoded = 0;
            BytesToHexStr( plotMemo, plotMemoSize, plotMemoStr, sizeof( plotMemoStr ) - 1, numEncoded );
            plotMemoStr[numEncoded*2] = 0;

            Log::Line( "Plot Memo: %s", plotMemoStr );
        }
        Log::Line( "" );

        PlotRequest req = {};
        req.plotId       = plotId;
        req.memo         = plotMemo;
        req.memoSize     = plotMemoSize;
        req.outDir       = plotOutFolder;
        req.plotFileName = plotFileName;
        req.plotOutPath  = plotOutPath;
        req.isFirstPlot  = i == 0;
        req.IsFinalPlot  = i == plotCount-1;

        plotter->Run( req );
    }
}

//-----------------------------------------------------------
void ParseCommandLine( GlobalPlotConfig& cfg, IPlotter*& outPlotter, int argc, const char* argv[] )
{
    CliParser cli( argc, argv );

    if( argc < 1 || cli.HasArgs( "-h", "--help" ) )
    {
        PrintUsage();
        exit( 0 );
    }

    if( cli.HasArgs( "-v", "--version" ) )
    {
        Log::Line( "Bladebit CUDA %s", BLADEBIT_VERSION );
        exit( 0 );
    }

    // Set the log level first
    if( cli.HasArgs( "-q", "--quiet" ) )
        Log::SetMaxDisplayLevel( LogLevel::Info );

    if( cli.HasArgs( "--verbose" ) )
        Log::SetMaxDisplayLevel( LogLevel::All );

    // Farmer & Pool public keys
    std::string farmerPkStr = "";
    std::string poolPkStr   = "";
    
    if( cli.GetArg( farmerPkStr, "-f", "--farmer-key" ) )
    {
        cfg.farmerPublicKey = new bls::G1Element();
        *cfg.farmerPublicKey = KeyTools::HexPKToG1Element( farmerPkStr.c_str() );
    }

    if( cli.GetArg( poolPkStr, "-p", "--pool-key" ) )
    {
        cfg.poolPublicKey = new bls::G1Element();
        *cfg.poolPublicKey = KeyTools::HexPKToG1Element( poolPkStr.c_str() );
    }

    // Pool contract puzzle hash
    std::string contractAddressStr = "";
    if( cli.GetArg( contractAddressStr, "-c", "--contract" ) )
    {
        if( !KeyTools::HexToBytes( contractAddressStr.c_str(), BB_PLOT_POOL_CONTRACT_PUZZLE_HASH_SIZE, cfg.poolContractPuzzleHash ) )
            Fatal( "Invalid contract puzzle hash." );
        
        cfg.poolPublicKey = nullptr; 
    }

    // Get plot count
    int32 plotCount = 1;
    cli.GetArg( plotCount, "-n", "--count" );
    cfg.plotCount = (uint32)Max( 1, plotCount );

    const char** commands     = cli.GetArgs();
    const uint32 commandCount = cli.ArgCount();
    
    if( commandCount < 1 )
    {
        PrintUsage();
        exit( 1 );
    }

    // Get command
    const char* command = commands[0];

    // CUDA-only commands
    if( strcmp( command, "cudaplot" ) == 0 )
    {
        auto* cudaPlotter = new CudaK32Plotter();
        cudaPlotter->ParseCLI( cfg, cli );
        outPlotter = cudaPlotter;
    }
    else if( strcmp( command, "memtest" ) == 0 )
    {
        MemTestMain( cfg, cli );
        exit( 0 );
    }
    else if( strcmp( command, "validate" ) == 0 )
    {
        PlotValidatorMain( cfg, cli );
        exit( 0 );
    }
    else if( strcmp( command, "help" ) == 0 )
    {
        if( commandCount < 2 )
        {
            PrintUsage();
            exit( 0 );
        }

        const char* helpCommand = commands[1];
        
        if( strcmp( helpCommand, "cudaplot" ) == 0 )
        {
            CudaK32PlotterPrintHelp();
        }
        else if( strcmp( helpCommand, "memtest" ) == 0 )
        {
            MemTestPrintUsage();
        }
        else if( strcmp( helpCommand, "validate" ) == 0 )
        {
            PlotValidatorPrintUsage();
        }
        else
        {
            Log::Line( "Unknown command '%s'.", helpCommand );
            PrintUsage();
            exit( 1 );
        }
        exit( 0 );
    }
    else
    {
        Log::Line( "Unknown command '%s'.", command );
        PrintUsage();
        exit( 1 );
    }

    // Validate required arguments
    if( !cfg.farmerPublicKey )
        Fatal( "A farmer public key must be specified with -f." );

    if( !cfg.poolPublicKey && !cfg.poolContractPuzzleHash[0] )
        Fatal( "Either a pool public key or a pool contract address must be specified with -p or -c, respectively." );

    // Get output directories
    const char** paths;
    uint32 pathCount;
    cli.GetArgs( paths, pathCount );

    if( pathCount < 2 )  // Command + at least 1 output dir
        Fatal( "An output directory must be specified." );

    // Last arguments are output directories
    for( uint32 i = 1; i < pathCount; i++ )
    {
        cfg.outputFolders[cfg.outputFolderCount++] = std::string( paths[i] );

        if( cfg.outputFolderCount >= GlobalPlotConfig::MAX_OUTPUT_FOLDERS )
            break;
    }
}

//-----------------------------------------------------------
void PrintUsage()
{
    Log::Line( "Bladebit CUDA %s", BLADEBIT_VERSION );
    Log::Line( "A high-performance CUDA k32-only Chia plotter" );
    Log::Line( "" );
    Log::Line( "USAGE:" );
    Log::Line( "  bladebit_cuda [GLOBAL_OPTIONS] <COMMAND> [COMMAND_OPTIONS] <output_directory>" );
    Log::Line( "" );
    Log::Line( "GLOBAL OPTIONS:" );
    Log::Line( "  -h, --help      : Show this help message and exit" );
    Log::Line( "  -v, --version   : Show version information and exit" );
    Log::Line( "  -f, --farmer-key <key> : Farmer public key (required)" );
    Log::Line( "  -p, --pool-key <key>   : Pool public key" );
    Log::Line( "  -c, --contract <address> : Pool contract address" );
    Log::Line( "  -n, --count <count>    : Number of plots to create (default: 1)" );
    Log::Line( "  -q, --quiet            : Only output errors and minimal information" );
    Log::Line( "      --verbose          : Enable verbose output" );
    Log::Line( "" );
    Log::Line( "COMMANDS:" );
    Log::Line( "  cudaplot    : Create plots using CUDA GPU acceleration" );
    Log::Line( "  memtest     : Test system memory" );
    Log::Line( "  validate    : Validate plot files" );
    Log::Line( "  help <cmd>  : Show command-specific help" );
    Log::Line( "" );
    Log::Line( "For command-specific options, use: bladebit_cuda help <command>" );
}