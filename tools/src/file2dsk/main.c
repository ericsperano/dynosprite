// File2DSK - Command line CoCo DSK creation utility
// Version 1.0.7 (May 26, 2006)
//
// Freeware, copyright 2006 by Patrick Benny (chipple.net)
// Inspired by DiskTape v1.1 copyright 1998 by Jean-Francois Morin
// http://www.chipple.net/coco/file2dsk/

// DESCRIPTION
// ~~~~~~~~~~~
// This utility creates a regular 35-track CoCo virtual disk DSK file
// for use in emulators that support the format.

// SYNTAX
// ~~~~~~
//  1. file2dsk <src_file1> [src_file2 .. [src_fileN]]
//     e.g. file2dsk program1.bin program2.bas (creates PROGRAM1.DSK)
//  2. file2dsk <dest_file.dsk> <src_file1> [src_file2 .. [src_fileN]]
//     e.g. file2dsk mydisk.dsk program1.bin program2.bas
//
// In Windows, files can also be simply drag-and-dropped onto this program.
//
// Running the program without any arguments will allow creating a DSK
// containing only one file by inputing its filename.

// FEATURES
// ~~~~~~~~
// - Automatically detects type and mode of BIN and binary BAS files
// - Allows specifying type and mode for non-detected files
// - Writes tracks in CoCo's standard order, from tracks 16-17 outwards

// SOME LIMITATIONS
// ~~~~~~~~~~~~~~~~
// - Doesn't create disks of formats other than the following:
//   1 side, 35 track, 18 sectors/trk, 256 bytes/sct
// - Cannot add files to existing disks. Writing to an existing DSK
//   will overwrite (after confirmation).
// - Filenames longer than XXXXXXXX.XXX are truncated, and
//   non-alphanumeric characters other than -#$%&'()@ [space] are removed.
// - It IS possible to put several files of the same name on a same DSK,
//   so be careful.

// VERSION HISTORY
// ~~~~~~~~~~~~~~~
// 1.0.7  2006/5/26  Now allows file type 3 (Text editor source file)
// 1.0.6  2006/5/22  Now allows characters -#$%&'()@ [space] in filenames
// 1.0.5  2006/4/24  Bug fix (created disks were always full)
// 1.0.4  2006/4/21  Now compiles on Linux (Thanks to mannequin!)
// 1.0.3  2006/4/20  Bug fix
// 1.0.2  2006/4/13  First public release

// Special thanks to Jean-Francois Morin for his utility DiskTape v1.1 that
// inspired me to create File2DSK. I used a few lines from DiskTape's code
// to get started, and borrowed logic to determine a file's type and mode.

// Thanks also to Mannequin for helping to make the source compilable on Linux,
// and to Jason Law for bug reporting.

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>


#define C_iFilenameMaxLen     200      // Maximum filename length

#define C_iDiskSize           0x27600  // Total disk size
#define C_iDiskNbGranules     68       // Number of granules
#define C_iDiskGATTrackPos    0x13200  // GAT track offset
#define C_iDiskGATTrackGATPos 0x100    // GAT offset within GAT track
#define C_iDiskGATTrackDirPos 0x200    // Dir offset within GAT track


// This block for implementing getch() for both Win and Linux
#ifdef WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>

// Function getch
//          ~~~~~
// Linux (possibly POSIX) replacement for Windows' "conio.h"
//
int getch (void)
{
	/* Ripped from                                             *
	 * <http://cboard.cprogramming.com/showthread.php?t=27714> */
	struct termios oldt, newt;
	int ch;

	tcgetattr (STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr (STDIN_FILENO, TCSANOW, &newt);
	ch = getchar ();
	tcsetattr (STDIN_FILENO, TCSANOW, &oldt);

	return ch;
}
#endif


// Function strtoupper
//          ~~~~~~~~~~
// Converts a string to uppercase.
//
// Parameters:
//    psz  String that will be converted to uppercase
//
// Returns a pointer to converted string (same as input) for convenience
//
char * strtoupper (char *psz) {
    int iLen = strlen(psz);  // Length of string
    int iPos;                // Counter for position
    for (iPos = 0; iPos < iLen; iPos++)
        psz[iPos] = toupper(psz[iPos]);
    return psz;
}

// Function AddFile
//          ~~~~~~~
// Add a file to the disk
//
// Parameters:
//    pszFilename  Pointer to the input fileame
//    fOut         Output DSK file handle
//    cLast        1 if last file, otherwise 0
//
// Returns 1 if ok to continue,
//      or 0 if error encountered (input file can't be open, disk full)
//
char AddFile(char *pszFilename, 
             FILE *fOut,
             char cLast) {

    // Static disk-related data
    static unsigned char szDisk[C_iDiskSize];
    static unsigned char szGATTrack[0x900*2];
    static const unsigned char szGATOrder[C_iDiskNbGranules] = {0x20,0x21,0x22,0x23,0x1e,0x1f,0x24,0x25,0x1c,0x1d,0x26,0x27,0x1a,0x1b,0x28,0x29,0x18,0x19,0x2a,0x2b,0x16,0x17,0x2c,0x2d,0x14,0x15,0x2e,0x2f,0x12,0x13,0x30,0x31,0x10,0x11,0x32,0x33,0x0e,0x0f,0x34,0x35,0x0c,0x0d,0x36,0x37,0x0a,0x0b,0x38,0x39,0x08,0x09,0x3a,0x3b,0x06,0x07,0x3c,0x3d,0x04,0x05,0x3e,0x3f,0x02,0x03,0x40,0x41,0x00,0x01,0x42,0x43};
    static unsigned char cCurGranule = 0;
    static unsigned char cNextGranule = 0;
    static int iFileNo = 0;

    FILE *fIn;                        // Input file handle
    int iBytesRead = 0;               // Number of bytes read from input file
    unsigned char cFirstGranule = 0;  // Index of first granule of current file
    long iPos;                        // Counter for loop when writing DSK

    char *pszTemp = NULL;             // Temp string pointer
    char cTemp;                       // One character
    int iFilenameLen = 0;             // Filename length
    int iAnswer;                      // User's answer to getch() question
    unsigned char cFileType,          // File type (0, 1, 2)
                  cFileMode;          // File mode (0, 255)

                                      // statFile for getting file size
#ifdef WIN32
    struct _stat statFile;
#else
    struct stat statFile;
#endif


    // Open input file
    if ((fIn = fopen(pszFilename, "rb")) == NULL) {
        perror("Error opening input file");
        return 0;
    }
    // Get file size
    fseek(fIn, 0, SEEK_SET);
#ifdef WIN32
    _stat(pszFilename, &statFile);
#else
    stat(pszFilename, &statFile);
#endif
    // If file size larger then number of space left on remaining granules,
    // display "Disk full" error and abort.
    if ((unsigned long)statFile.st_size > (sizeof(szGATOrder)-cCurGranule)*0x900) {
        printf("Disk full.\n");
        return 1;
    }

    // If first file, initialize disk
    if (iFileNo == 0) {
        // Initialize whole disk with 0xff, GAT index sector with 0x00, GAT index with 0xFF
        memset(szDisk,0xff,sizeof(szDisk));
        memset(&szGATTrack[C_iDiskGATTrackGATPos],0x00,0x100);
        memset(&szGATTrack[C_iDiskGATTrackGATPos],0xff,C_iDiskNbGranules);
    }

    // Copy file to disk, one granule at a time
    cFirstGranule = cNextGranule;
    while (!ferror(fIn) && !feof(fIn) && cNextGranule < sizeof(szGATOrder)) {
        cCurGranule = cNextGranule;
        iBytesRead = fread(&szDisk[szGATOrder[cNextGranule]*0x900], sizeof(unsigned char), 0x900, fIn);
        cNextGranule++;
        szGATTrack[0x100+szGATOrder[cCurGranule]] = !feof(fIn) ? szGATOrder[cNextGranule] : (0xc1+iBytesRead/0x100);
    }

    // Close input file
    fclose(fIn);


    // File type
    // first, try to guess type from name
    char *pszExtension = pszFilename + strlen(pszFilename) - 3;
    if (strcmp(pszExtension, "BAS") == 0)
    {
        printf("File type: BASIC. File mode: ASCII.\n");
        cFileType = 0;
        cFileMode = 0xff;
    }
    else if (strcmp(pszExtension, "BIN") == 0)
    {
        printf("File type: Machine language. File mode: Binary.\n");
        cFileType = 2;
        cFileMode = 0;
    }
    else if (strcmp(pszExtension, "DAT") == 0)
    {
        printf("File type: Data. File mode: Binary.\n");
        cFileType = 1;
        cFileMode = 0;
    }
    else
    {
    // Evaluate file type from first character read
    switch (szDisk[szGATOrder[cFirstGranule]*0x900]) {
        case 0x00:  // Machine language / Binary
            printf("File type: Machine language. File mode: Binary.\n");
            cFileType = 2;
            cFileMode = 0;
            break;
        case 0xff:  // BASIC / Binary
            printf("File type: BASIC. File mode: Binary.\n");
            cFileType = 0;
            cFileMode = 0;
            break;

        default:
        
            printf("File type unknown (might be an ASCII BASIC program or a data file).\n");
            // Query file type
            printf("Enter file type (0 = BASIC, 1 = Data, 2 = ML, 3 = Text): ");
            while ((iAnswer = getch()) < '0' || iAnswer > '3') {
                if (iAnswer == -1 || iAnswer == 3) {
                    printf("Aborted.");
                    return 0;
                }
            }
            // Evaluate file type from user input
            cFileType = iAnswer-'0';
            printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");
            printf("File type:                                              ");
            printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");
            switch (cFileType) {
                case 0x00: printf("BASIC. "); break;
                case 0x01: printf("Data. ");  break;
                case 0x02: printf("Machine language. ");    break;
                case 0x03: printf("Text. ");    break;
            }

            // Input file mode
            switch (cFileType) {
                case 0x00:
                case 0x01:
                case 0x03:
                    printf("Enter file mode (0 = Binary, [1] = ASCII): ");
                    break;
                case 0x02:
                    printf("Enter file mode ([0] = Binary, 1 = ASCII): ");
                    break;
            }
            while ((iAnswer = getch()) != '0'  // Binary
                && iAnswer != '1'              // ASCII
                && iAnswer != 13               // Enter (default)
                && iAnswer != 32) {            // Space (default)
                if (iAnswer == -1 || iAnswer == 3) {
                    printf("Aborted.");
                    return 0;
                }
            }
            switch (iAnswer) {
                case '0':  // Binary
                    cFileMode = 0;
                    break;
                case '1':  // ASCII
                    cFileMode = (char)0xff;
                case 13:   // Enter (default)
                case 32:   // Space (default)
                    switch (cFileType) {
                        case 0x00: 
                        case 0x01:
                        case 0x03:
                            cFileMode = (char)0xff;
                            break;
                        case 0x02:
                            cFileMode = 0;
                            break;
                    }
                    break;
            }
            printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");
            printf("File mode:                                ");
            printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");
            switch (cFileMode) {
                case 0x00: printf("Binary.\n"); break;
                case 0xff: printf("ASCII.\n");  break;
            }

            break;
    }
    }
    printf("\n");


    // Initialize disk's GAT directory file data with 0x00
    memset(&szGATTrack[C_iDiskGATTrackDirPos+iFileNo*0x20],0x00,0x20);

    // Copy filename to GAT directory file data
    // Make filename uppercase
    strtoupper(pszFilename);
    // Cut after last slash
    if ((pszTemp = strrchr(pszFilename, '/')) == NULL)
        pszTemp = pszFilename;
    else
        pszTemp = pszTemp++;
    // Write filename
    while ((cTemp = pszTemp[0]) != '.' && cTemp != 0x00 && iFilenameLen < 8) {
        cTemp = toupper(cTemp);
        if (strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#$%&'()@ ",cTemp) != NULL)
            szGATTrack[C_iDiskGATTrackDirPos+iFileNo*0x20+(iFilenameLen++)] = cTemp;
        pszTemp++;
    }
    while (iFilenameLen < 8)
        szGATTrack[C_iDiskGATTrackDirPos+iFileNo*0x20+(iFilenameLen++)] = ' ';
    if ((pszTemp = strrchr(pszTemp,'.')) != NULL) {
        pszTemp++;
        while ((cTemp = pszTemp[0]) != 0x00 && iFilenameLen < 11) {
            cTemp = toupper(cTemp);
            if (strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#$%&'()@ ",cTemp) != NULL)
                szGATTrack[C_iDiskGATTrackDirPos+iFileNo*0x20+(iFilenameLen++)] = cTemp;
            pszTemp++;
        }
    }
    while (iFilenameLen < 11)
        szGATTrack[C_iDiskGATTrackDirPos+iFileNo*0x20+(iFilenameLen++)] = ' ';

    // Set file type
    // BASIC -> 0x00, Data -> 0x01, ML -> 0x02
    szGATTrack[C_iDiskGATTrackDirPos+iFileNo*0x20+0x0b] = cFileType;
    // Set file mode (correct pos?)
    // ASCII -> 0x00, Binary -> 0xff
    szGATTrack[C_iDiskGATTrackDirPos+iFileNo*0x20+0x0c] = cFileMode;
    // Set file first granule
    szGATTrack[C_iDiskGATTrackDirPos+iFileNo*0x20+0x0d] = szGATOrder[cFirstGranule];
    // Set file last granule length (MSB)
    szGATTrack[C_iDiskGATTrackDirPos+iFileNo*0x20+0x0e] = (char)(iBytesRead != 0 && iBytesRead % 0x100 == 0 ? 0x01 : 0x00);
    // Set file last granule length (LSB)
    szGATTrack[C_iDiskGATTrackDirPos+iFileNo*0x20+0x0f] = (char)(iBytesRead % 0x100);

    iFileNo++;


    // Write disk
    if (cLast == 1) {
        // Move 0x13200+ of one track to make room for GAT
        memmove(
            &szDisk[C_iDiskGATTrackPos+0x900*2],
            &szDisk[C_iDiskGATTrackPos],
            sizeof(szDisk)-C_iDiskGATTrackPos-0x900*2);

        // Copy GAT track to disk
        memcpy(&szDisk[C_iDiskGATTrackPos],szGATTrack,sizeof(szGATTrack));

        for (iPos = 0; iPos < sizeof(szDisk); iPos++)
            fprintf(fOut, "%c", szDisk[iPos]);
    }

    return 1;
}

// Main function
int main(int argc, char **argv) {
    FILE *outfile;            // Output file handle (for writing DSK)
    FILE *infile = NULL;      // Input file handle (for testing existance)
    char szInFile[C_iFilenameMaxLen+1],     // Input filename (when user-input)
         szOutFile[C_iFilenameMaxLen+4+1],  // Output DSK filename
         szUpperOutFile[C_iFilenameMaxLen+4+1];
    int iArg;                 // Counter to loop through arguments
    char cContinue = 1;       // Set to 0 if adding a file fails
    char *pszExt;             // Pointer to extension within filename
    char cInputStartArg = 1;  // Start argument containing input filename
                              //  (changed to 2 if DSK output file specified)
    int iAnswer;              // User's answer to getch() question
    char cFirstTry = 1;       // 1 if first try inputting filename, otherwise 0

    // Display header
    printf("File2DSK - Command line CoCo DSK creation utility\n");
    printf("Version 1.0.7X (2014)\n");
    printf("Freeware, copyright 2006 by Patrick Benny\n");
    printf("Patched by Richard Goedeken for Unix filename case sensitivity, path separators, and automatic file type detection.\n");
    printf("Inspired by DiskTape v1.1 copyright 1998 by Jean-Francois Morin\n\n");
    printf("http://www.chipple.net/coco/file2dsk/\n");

    printf("This utility creates a regular 35-track CoCo virtual disk DSK file.\n\n");
    
    if (argc <= 1) {
        // Display instructions
        printf("Syntax:\n");
        printf("  1. file2dsk <src_file1> [src_file2 .. [src_fileN]]\n");
        printf("     e.g. file2dsk program1.bin program2.bas (creates PROGRAM1.DSK)\n");
        printf("  2. file2dsk <dest_file.dsk> <src_file1> [src_file2 .. [src_fileN]]\n");
        printf("     e.g. file2dsk mydisk.dsk program1.bin program2.bas\n");
        printf("In Windows, input files can also be simply drag-and-dropped onto this program.\n\n");

        printf("To create a DSK with only one file, enter filename. (Ctrl-C to quit)\n");

        // Ask for filename until a valid filename is input
        do {
            // Display error message if not first try
            if (cFirstTry == 0)
                perror("Error opening input file");
            else
                cFirstTry = 0;
            // Get user input
            printf("Input file (BIN, BAS, etc.): ");
            fgets(szInFile, C_iFilenameMaxLen, stdin);
            // Remove trailing new line
            szInFile[strlen(szInFile)-1] = '\0';
        }
        while (szInFile[0] != '\0' && (infile = fopen(szInFile, "rb")) == NULL);
        if (infile != NULL)
            fclose(infile);

        // If blank string input, abort
        if (szInFile[0] == '\0') {
            printf("Aborted.");
            exit(1);
        }
        strcpy(szOutFile,strtoupper(szInFile));
    }
    else {
        // Make all arguments uppercase
        //for (iArg = 2; iArg < argc && cContinue == 1; iArg++)
        //    strtoupper(argv[iArg]);
        // Set output filename
        strcpy(szOutFile,argv[1]);
        strcpy(szUpperOutFile, argv[1]);
        strtoupper(szUpperOutFile);
    }

    // Adjust output file name
    if (strstr(szUpperOutFile,".DSK") != NULL)
        cInputStartArg++;
    else {
        if ((pszExt = strrchr(szOutFile,'.')) == NULL)
            strcat(szOutFile,".DSK");
        else
            strcpy(pszExt,".DSK");
    }
    printf("Output DSK file: %s\n", szOutFile);

    // Check whether output file exists
    if ((outfile = fopen(szOutFile, "r")) != NULL) {
        fclose(outfile);
        printf("DSK file already exists, overwrite? Y/[N] ");
        while ((iAnswer = toupper(getch())) != 'Y'
            && iAnswer != 'N' 
            && iAnswer != 13    // Enter
            && iAnswer != 32    // Space
            && iAnswer != 3     // CTRL+C
            && iAnswer != -1);  // Break?
        if (iAnswer != 'Y') {
            printf("Aborted.");
            exit(1);
        }
        printf("\x8\x8\x8\x8\x8\x8Y    \n\n");
    }

    // Open output file
    if ((outfile = fopen(szOutFile, "wb")) == NULL) {
        perror("Error opening output file");
        exit(1);
    }

    // Add input file(s)
    if (argc <= 1)
        cContinue = AddFile(szInFile, outfile, 1);
    else {
        for (iArg = cInputStartArg; iArg < argc && cContinue == 1; iArg++) {
            printf("Input file: %s\n", argv[iArg]);
            cContinue = AddFile(argv[iArg], outfile, (char)((iArg==argc-1)?1:0));
        }
    }

    // Close output file
    fclose(outfile);

    // Check if disk creation successful
    if (cContinue == 1)
        printf("Done.\n");
    else {
        // Delete output file
        unlink(szOutFile);
        printf("Aborted.\n");
    }

    exit(0);
}
