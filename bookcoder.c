/*
 * A digital version of the book cipher.
 * 
 * With the classical book cipher, Alice maps each word of her message to a corresponding page and
 * paragraph number where the word appears in a book. The list of numbers becomes the book code, and
 * later Bob uses the numbers to look up the words in the book to extract Alice's message.
 * 
 * In this digital version, each byte of the original file is mapped to the offset in a book file 
 * where a corresponding byte resides, and the offset is then written to a file to become the book 
 * code. The program can then read the offsets from the book code, and seek to those positions in 
 * the book file and write the byte residing at that location to extract the original file.
 * 
 * With the original book cipher, Alice would use a book that is commonly and easily available, 
 * perhaps in a public library, and tell Bob which book was used and where to find it. Similarly, 
 * the digital version could use a file that is widely mirrored online to serve as the book file.
 * 
 * The offsets from the book file are written as 32-bit integers. Book files that are larger than 4 
 * GB can still be used, since bytes can still be mapped to an offset within the 32-bit range. 
 * Unfortunately, this also means that for every byte of the original file, 4 bytes are stored 
 * making the book code 4 times as large as the original file. Fortunately, because most of the 
 * least-significant bits of the 32-bit integers will be null, most of those 4 bytes will also 
 * be null and heavily compressible.
 * 
 * If 32-bit integers are not sufficient to map offset sizes required, the code can be modified to
 * change the uoffset_t and offset_t types to use 64-bit integers instead of 32-bit integers. This
 * will of course result in book code files that are 8 times larger instead of 4. In my testing I 
 * have not found this necesary, so have restricted it to 32-bit integers.
 * 
 */
 
/*These macros will print various errors. They are defined here so that 
 * the line numbers will be accurate upon their expansion*/ 
#define PRINT_SYS_ERROR(errCode) \
    { \
        fprintf(stderr, "%s:%s:%d: %s\n", __FILE__, __func__, __LINE__, strerror(errCode)); \
    }

#define PRINT_FILE_ERROR(fileName, errCode) \
    { \
        fprintf(stderr, "%s: %s (Line: %i)\n", fileName, strerror(errCode), __LINE__); \
    }

#define PRINT_ERROR(errMsg) \
    { \
        fprintf(stderr, "%s:%s:%d: %s\n", __FILE__, __func__, __LINE__, errMsg); \
    }

#define _FILE_OFFSET_BITS 64

/* This defines a 1 MB buffer to be used by default. */
#define DEFAULT_BUFFER_SIZE 1024 * 1024

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <sys/stat.h>

typedef uint32_t uoffset_t;
typedef int32_t offset_t;
typedef uint8_t byte_t;

struct bookFileStruct {
    FILE *bkFil;
    char bkFilName[NAME_MAX];
    byte_t bkFilByte;
    size_t bkFilSize;
    byte_t *bkFilBuffer;
    size_t bkFilBufSize;
    uoffset_t bkFilPos;
    uoffset_t bkFilBufPos;
};

struct bookCodeStruct {
    FILE *bkCd;
    char bkCdFilName[NAME_MAX];
    size_t bkCdSize;
    uoffset_t *bkCdBuffer;
    size_t bkCdBufSize;
};

struct originalFileStruct {
    FILE* orgFil;
    char orgFilName[NAME_MAX];
    byte_t orgFilByte;
    size_t orgFilSize;
    byte_t *orgFilBuffer;
    size_t orgFilBufSize;
    uoffset_t orgFilBufPos;
};

struct extractedFileStruct {
    FILE *extrFil;
    char extrFilName[NAME_MAX];
    byte_t *extrFilBuffer;
    size_t extrFilBufSize;
    uoffset_t extrFilBufPos;
};

struct offsetStruct {
    uoffset_t byteOffset;
    offset_t offsetDigest[256];
};

struct optionsStruct {
    bool mapOffsets;
    bool extractBytes;
    bool bkFilGiven;
    bool bkCdGiven;
    bool orgFilGiven;
    bool outputFileGiven;
    bool orgFilBufSizeGiven;
    bool bkCdBufSizeGiven;
    bool bkFilBufSizeGiven;
    bool extrFilBufSizeGiven;
    bool allowDuplicates;
    bool writeToStdout;
    bool readFromStdin;
    bool resetAtEndOfBuf;
    int verbosityLevel;  
};

/*A wrapper to run fread with error checking*/
int freadWErrCheck(void *ptr, size_t size, size_t nmemb, FILE *stream, int *returnVal)
{
    /* 
	 * From glibc manual: On  success,  fread()  and fwrite() return the number of items read or 
     * written.  This number equals the number of bytes transferred only when size is 1.  If an 
     * error occurs, or the end of the file is reached, the return value is a short item count 
     * (or zero).
	 * 
	 * The number of items read/written will always equal nmemb / size 
	 * unless EOF was reached before that, or if some other error occured 
	 */
    if (fread(ptr, size, nmemb, stream) != nmemb / size) {
        if (feof(stream)) {
            *returnVal = EBADMSG;
            return EBADMSG;
        } else if (ferror(stream)) {
            *returnVal = errno;
            return errno;
        }
    }

    return 0;
}

/*A wrapper to run fwrite with error checking*/
int fwriteWErrCheck(void *ptr, size_t size, size_t nmemb, FILE *stream, int *returnVal)
{
    if (fwrite(ptr, size, nmemb, stream) != nmemb / size) {
        if (feof(stream)) {
            *returnVal = EBADMSG;
            return EBADMSG;
        } else if (ferror(stream)) {
            *returnVal = errno;
            return errno;
        }
    }

    return 0;
}

int getBufSizeMultiple(char *value) { 
    
    #define MAX_DIGITS 13
    char valString[MAX_DIGITS] = {0};
    /* Compiling without optimization results in extremely slow speeds, but this will be optimized 
     * out if not set to volatile.
     */
    volatile int valueLength = 0;
    volatile int multiple = 1;
    
    /* value from getsubopt is not null-terminated so must copy and get the length manually without
     * string functions
     */
    for(valueLength = 0;valueLength < MAX_DIGITS;valueLength++) {
        if(isdigit(value[valueLength])) {
            valString[valueLength] = value[valueLength];
            continue;
        }
        else if(isalpha(value[valueLength])) {
            valString[valueLength] = value[valueLength];
            valueLength++;
            break;
        }
    }
    
    if(valString[valueLength-1] == 'b' || valString[valueLength-1] == 'B')
        multiple = 1;
    if(valString[valueLength-1] == 'k' || valString[valueLength-1] == 'K')
        multiple = 1024;
    if(valString[valueLength-1] == 'm' || valString[valueLength-1] == 'M')
        multiple = 1024*1024;
        
    return multiple;
}

int mapOffsets(
struct bookFileStruct *bkFilSt,
struct bookCodeStruct *bkCdSt, 
struct originalFileStruct *orgFilSt,
struct offsetStruct *oSetSt,
struct optionsStruct *optSt
)
{
    int returnVal = 0;
    int repeatsFound = 0;
    
    /*Prime the bkFilBuffer before starting the loop*/
    if(freadWErrCheck(bkFilSt->bkFilBuffer, 1, sizeof(byte_t) * bkFilSt->bkFilBufSize, bkFilSt->bkFil, &returnVal) != 0) {
        PRINT_SYS_ERROR(returnVal);
        exit(EXIT_FAILURE);
    }

    /* Begin mapping the original file to offsets in the book file */
    size_t bytesRemaining = orgFilSt->orgFilSize;
    size_t currentChunk = orgFilSt->orgFilBufSize;
    for (; bytesRemaining; bytesRemaining -= currentChunk) {
        if(currentChunk > bytesRemaining) {
            currentChunk = bytesRemaining;
        }

        if(freadWErrCheck(orgFilSt->orgFilBuffer, 1, sizeof(byte_t) * currentChunk, orgFilSt->orgFil, &returnVal) !=0 ) {
            PRINT_SYS_ERROR(returnVal);
            exit(EXIT_FAILURE);
        }
        
        if(optSt->verbosityLevel >= 2) {
            fprintf(stderr,"Processing chunk %lu-%lu of original file...\n", ftell(orgFilSt->orgFil) - currentChunk, ftell(orgFilSt->orgFil));
        }

        orgFilSt->orgFilBufPos = 0;

    /* We will need to jump to the head of this loop if a byte is mapped, but because the comparison
     * is within a nested loop, a break procedure will not work so a goto is needed
    */
    getNextOriginalFileByte:
        while (orgFilSt->orgFilBufPos < currentChunk) {

            orgFilSt->orgFilByte = orgFilSt->orgFilBuffer[orgFilSt->orgFilBufPos];

            while (bkFilSt->bkFilPos < bkFilSt->bkFilSize) {

                for (; bkFilSt->bkFilBufPos < bkFilSt->bkFilBufSize; bkFilSt->bkFilBufPos++) {

                    bkFilSt->bkFilByte = bkFilSt->bkFilBuffer[bkFilSt->bkFilBufPos];
                    
                    if (orgFilSt->orgFilByte == bkFilSt->bkFilByte) {

                        oSetSt->byteOffset = bkFilSt->bkFilBufPos + bkFilSt->bkFilPos;

                        if(!optSt->allowDuplicates) {
                            /* This will check if offset for the book file byte has been previously 
                             * indexed already in order to prevent repeats.
                             */
                            if (oSetSt->offsetDigest[bkFilSt->bkFilByte] == oSetSt->byteOffset) {
                                
                                /* If repeatsFound is 2 or over then we cannot avoid a repeat in the 
                                 * buffer and should refill it below
                                 */
                                if(repeatsFound >= 2) {
                                    goto refillBuffer;
                                }               
                                                 
                                /* Have to be sure to start back at the beginning of the buffer if we 
                                 * have reached the end
                                 */
                                if (bkFilSt->bkFilBufPos == (bkFilSt->bkFilBufSize - 1)) {
                                    bkFilSt->bkFilBufPos = 0;
                                }
                                
                                repeatsFound++;
                                continue;
                            }
                        }
                        
                        repeatsFound = 0;

                        /* This will index the offset for the book file byte found in order to be 
                         * checked next time around.
                         */
                        oSetSt->offsetDigest[bkFilSt->bkFilByte] = oSetSt->byteOffset;

                        if(fwriteWErrCheck(&oSetSt->byteOffset, 1, sizeof(oSetSt->byteOffset), bkCdSt->bkCd, &returnVal) != 0) {
                            PRINT_SYS_ERROR(returnVal);
                            exit(EXIT_FAILURE);
                        }
                        
                        if(optSt->verbosityLevel >= 3) {
                            fprintf(stderr,"Wrote offset %lu\n", (uint64_t)oSetSt->byteOffset);
                        }

                        orgFilSt->orgFilBufPos++;

                        goto getNextOriginalFileByte;
                    } else if ((orgFilSt->orgFilByte != bkFilSt->bkFilByte) && bkFilSt->bkFilBufPos == (bkFilSt->bkFilBufSize - 1)) {
                        
                        refillBuffer:
                        /* Increment the book file position and reset the buffer position */
                        bkFilSt->bkFilPos += bkFilSt->bkFilBufSize;
                        bkFilSt->bkFilBufPos = 0;
                         
                        /* If we have reached the end of the book file or reset-at-buffer is set */
                        if (bkFilSt->bkFilPos >= (bkFilSt->bkFilSize - 1) || optSt->resetAtEndOfBuf) {

                            /* This will check if the file byte was not able to be mapped to any 
                             * byte in the book file, in which case we we will abort.
                             */
                            if (oSetSt->offsetDigest[orgFilSt->orgFilByte] == -1) {
                                fprintf(stderr,"Not enough entropy in book file or book buffer, book code could not be created\n");
                                exit(EXIT_FAILURE);
                            }
                            
                            /*Reset to the beginning of the book file to fill the buffer*/
                            bkFilSt->bkFilPos = 0;
                            fseek(bkFilSt->bkFil, 0, SEEK_SET);
                        }
                        
                        /* Refill the book file buffer with the next chunk */
                        if(freadWErrCheck(bkFilSt->bkFilBuffer, 1, sizeof(byte_t) * bkFilSt->bkFilBufSize, bkFilSt->bkFil, &returnVal) != 0) {
                            PRINT_SYS_ERROR(returnVal);
                            exit(EXIT_FAILURE);
                        }
                    }
                }
            }
        }
    }

    return 0;
}

int extractBytes(
struct bookFileStruct *bkFilSt,
struct bookCodeStruct *bkCdSt, 
struct extractedFileStruct *extrFilSt,
struct offsetStruct *oSetSt,
struct optionsStruct *optSt
)
{
    int returnVal = 0;
    
    size_t currentChunk;
    
    while (1) {
        
        currentChunk = fread(bkCdSt->bkCdBuffer,sizeof(byte_t),bkCdSt->bkCdBufSize,bkCdSt->bkCd);
        
        if(currentChunk < bkCdSt->bkCdBufSize && ferror(bkCdSt->bkCd)) {
            PRINT_FILE_ERROR(bkCdSt->bkCdFilName,errno);
            exit(EXIT_FAILURE);
        }
        
        if(optSt->verbosityLevel >= 2) {
            fprintf(stderr,"Processing chunk %lu-%lu of original file...\n", ftell(bkCdSt->bkCd) - currentChunk, ftell(bkCdSt->bkCd));
        }    

        /* Every uoffset_t sized chunk of the book code represents 1 byte of the original file, so 
         * we need to divide currentChunk by the size of the byteOffset (which will be equal to 
         * uoffset_t) to match each offset to a  byte in the extracted file buffer.
         */
        for (extrFilSt->extrFilBufPos = 0; extrFilSt->extrFilBufPos < currentChunk / sizeof(oSetSt->byteOffset); extrFilSt->extrFilBufPos++) {
            
            /* Use the extracted file buffer position to index the offsets in the book code buffer 
             * and seek to said offset in the book file.
             */
            fseek(bkFilSt->bkFil, bkCdSt->bkCdBuffer[extrFilSt->extrFilBufPos], SEEK_SET);
            
            /* Grab the byte residing at that offset in the book file and copy it into the 
             * extracted file buffer
             */
            extrFilSt->extrFilBuffer[extrFilSt->extrFilBufPos] = fgetc(bkFilSt->bkFil);
        }
        
        if(optSt->verbosityLevel >= 3) {
            fprintf(stderr,"Extracted byte at offset %lu", (uint64_t)bkCdSt->bkCdBuffer[extrFilSt->extrFilBufPos]);
        }    

        /* Again, currentChunk needs to be divided by the size of the byteOffset to accurately 
         * represent how many bytes to write
         */
        if(fwriteWErrCheck(extrFilSt->extrFilBuffer, 1, currentChunk / sizeof(oSetSt->byteOffset), extrFilSt->extrFil, &returnVal) != 0) {
            PRINT_SYS_ERROR(returnVal);
            exit(EXIT_FAILURE);
        }
        
        if(currentChunk < bkCdSt->bkCdBufSize && feof(bkCdSt->bkCd)) {
            break;
        }
        
    }
    
    return 0;
}

void printHelp(char *argv) {
    fprintf(stderr, 
"Syntax:\n%s -m | -e -b 'book file' [-c 'book code'] | -o 'original file' [-f 'output file'] [-p] [-r] [-d] [-s] [-v]\n\
\nOptions:\
\n\t-m,--map - Map bytes of of original file into book code\
\n\t\t-b,--book-file 'book file'\n\
\n\t\t-o,--original-file 'original file'\n\
\n\t\t-f,--output-file 'output file'\n\
\n\t\t-p,--stdio - Pipe book code to standard output instead of to file.\n\
\n\t\t-r,--reset-at-buffer - Reset and begin reading at the beginning of the book file when the end of the buffer is reached. This can help reduce file size after compression.\
\n\t\t Note: The 'book_file_buffer' buffer may not have enough entropy to avoid repeats and duplicates. You can increase its size with -s.\n\
\n\t\t-d,--duplicates - Allows using duplicate/repeat offsets.\n\
\n\t\t-s,--bufer-size - Comma separated list of buffer sizes. Suffix with 'b' for bytes, 'k' for kilobytes, or 'm' for megabytes. Defaults to 1m.\
\n\t\t\t book_file_buffer=num[b|k|m]\
\n\t\t\t\t Controls what size chunk of the book file will be loaded into memory at a time.\
\n\t\t\t\t Note: If set too low, the buffer may not have enough entropy to avoid repeats and duplicates if -r is also set.\n\
\n\t\t\t original_file_buffer=num[b|k|m]\
\n\t\t\t\t Controls what size chunk of the original file will be loaded into memory at a time\n\
\n\t\t-v,--verbosity-level 'n' - Sets verbosity level to 1, 2, or 3. (Notice, Info, Debug)\n\
\n\t-e,--extract - Extract bytes of original file from book code\
\n\t\t-b,--book-file 'book file'\n\
\n\t\t-c,--book-code 'book code'\n\
\n\t\t-p,--stdio - Pipe book code in from standard input instead of from file.\n\
\n\t\t-f,--output-file 'output file'\n\
\n\t\t-s,--buffer-size - Comma separated list of buffer sizes. Suffix with 'b' for bytes, 'k' for kilobytes, or 'm' for megabytes. Defaults to 1m.\
\n\t\t\t book_code_buffer=num[b|k|m]\
\n\t\t\t\t Controls what size chunk of the book code will be loaded into memory at a time\
\n\t\t\t extracted_file_buffer=num[b|k|m]\
\n\t\t\t\t Controls what size chunk of the extracted file will be held in memory before writing to disk\n\
\n\t\t-v,--verbosity-level 'n' - Sets verbosity level to 1, 2, or 3. (Notice, Info, Debug)\n\
\nExamples:\
\nMap a book code from an original file named 'orginal_file' using a book file named 'book_file' and write to a file named 'book code' using 512 kilobyte buffers\
\n\tbookcoder -m -b book_file -o original_file -f book_code -s original_file_buffer=512k,book_file_buffer=512k\
\n\nExtract a file from a book code named 'book_code' using a book file named 'book_file' and write to a file named 'original_file' using a 1 megabyte offset buffer\
\n\tbookcoder -e -b book_file -c book_code -f original_file -s book_code_buffer=1m\n\
\nMap a book code from an original file named 'original_file' using a book file named 'book_file' and pipe to 7zip to write book code to 'book_code.7z'\
\n\tbookcoder --map --book-file book_file --original_file original_file --stdio | 7z a -si ./book_code.7z\n\
\nPipe book code in from a file namd 'book_code.7z' and use a book file named 'book_file' to extract and write to a file namd 'original_file'\
\n\t7z x -so ./book_code.7z | bookcoder --extract --book-file book_file --output_file original_file --stdio\
\n", argv);
}

void parseOptions(
int argc,
char *argv[],
struct bookFileStruct *bkFilSt,
struct bookCodeStruct *bkCdSt,
struct originalFileStruct *orgFilSt,
struct extractedFileStruct *extrFilSt,
struct offsetStruct *oSetSt,
struct optionsStruct *optSt
) {
    int c;
    int errflg = 0;
    char binName[NAME_MAX];
    snprintf(binName,NAME_MAX,"%s",argv[0]);

    /*Process through arguments*/
    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"map",               no_argument,       0,'m' },
            {"extract",           no_argument,       0,'e' },
            {"book-file",         required_argument, 0,'b' },
            {"book-code",         required_argument, 0,'c' },
            {"original-file",     required_argument, 0,'o' },
            {"output-file",       required_argument, 0,'f' },
            {"buffer-size",       required_argument, 0,'s' },
            {"verbose",           required_argument, 0,'v' },
            {"help",              no_argument,       0,'h' },
            {"duplicates",        no_argument,       0,'d' },
            {"stdio",             no_argument,       0,'p' },
            {"reset-after-buffer",no_argument,       0,'r' },
            {0,                0,                 0, 0  }
        };
        
        c = getopt_long(argc, argv, "meb:c:o:f:s:v:hpr",
                        long_options, &option_index);
       if (c == -1)
           break;

        switch (c) {
        
        case 'm':
            optSt->mapOffsets = true;
        break;
        case 'e':
            optSt->extractBytes = true;
        break;
        case 'b':
            if (optarg[0] == '-' && strlen(optarg) == 2) {
                fprintf(stderr, "Option -b requires an argument\n");
                errflg++;
                break;
            } else {
                optSt->bkFilGiven = true;
                snprintf(bkFilSt->bkFilName, NAME_MAX, "%s", optarg);
            }
        break;
        case 'c':
            if (optarg[0] == '-' && strlen(optarg) == 2) {
                fprintf(stderr, "Option -c requires an argument\n");
                errflg++;
                break;
            } else {
                optSt->bkCdGiven = true;
                snprintf(bkCdSt->bkCdFilName, NAME_MAX, "%s", optarg);
            }
        break;
        case 'o':
            if (optarg[0] == '-' && strlen(optarg) == 2) {
                fprintf(stderr, "Option -o requires an argument\n");
                errflg++;
                break;
            } else {
                optSt->orgFilGiven = true;
                snprintf(orgFilSt->orgFilName, NAME_MAX, "%s", optarg);
            }
        break;
        case 'f':
            if (optarg[0] == '-' && strlen(optarg) == 2) {
                fprintf(stderr, "Option -f requires an argument\n");
                errflg++;
                break;
            } else {
                if(optSt->mapOffsets == true) {
                    optSt->outputFileGiven = true;
                    snprintf(bkCdSt->bkCdFilName, NAME_MAX, "%s", optarg);
                }
                else if(optSt->extractBytes == true) {
                    optSt->outputFileGiven = true;
                    snprintf(extrFilSt->extrFilName, NAME_MAX, "%s", optarg);
                }
            }
        break;
        case 's':
            if (optarg[0] == '-' && strlen(optarg) == 2) {
                fprintf(stderr, "Option -s requires an argument\n");
                errflg++;
                break;
            } else {
                enum {
                    ORIG_FILE_BUFFER = 0,
                    BOOK_FILE_BUFFER,
                    BKCD_FILE_BUFFER,
                    EXTR_FILE_BUFFER
                };

                char *const token[] = {
                    [ORIG_FILE_BUFFER]   = "original_file_buffer",
                    [BOOK_FILE_BUFFER]   = "book_file_buffer",
                    [BKCD_FILE_BUFFER]   = "book_code_buffer",
                    [EXTR_FILE_BUFFER]   = "extracted_file_buffer",
                    NULL
                };
                
                char *subopts;
                char *value;
                
                subopts = optarg;
                while (*subopts != '\0' && !errflg) {
                    switch (getsubopt(&subopts, token, &value)) {
                    case ORIG_FILE_BUFFER:
                        if (value == NULL) {
                            fprintf(stderr, "Missing value for suboption '%s'\n", token[ORIG_FILE_BUFFER]);
                            errflg = 1;
                            continue;
                        }
                        
                        if(!optSt->mapOffsets) {
                            fprintf(stderr,"original_file_buffer will have no effect when extracting bytes\n");
                        }
                        
                        optSt->orgFilBufSizeGiven = true;
                        orgFilSt->orgFilBufSize = atol(value) * sizeof(byte_t) * getBufSizeMultiple(value);
                    break;
                    case BOOK_FILE_BUFFER:
                        if (value == NULL) {
                            fprintf(stderr, "Missing value for suboption '%s'\n", token[BOOK_FILE_BUFFER]);
                            errflg = 1;
                            continue;
                        }
                        
                        if(!optSt->mapOffsets) {
                            fprintf(stderr,"book_file_buffer will have no effect when extracting bytes\n");
                        }
                            
                        optSt->bkFilBufSizeGiven = true;
                        bkFilSt->bkFilBufSize = atol(value) * sizeof(byte_t) * getBufSizeMultiple(value);
                    break;
                    case BKCD_FILE_BUFFER:
                        if (value == NULL) {
                            fprintf(stderr, "Missing value for "
                            "suboption '%s'\n", token[BKCD_FILE_BUFFER]);
                            errflg = 1;
                            continue;
                        }
                        
                        if(!optSt->extractBytes) {
                            fprintf(stderr,"book_code_buffer will have no effect when mapping offsets\n");
                        }

                        optSt->bkCdBufSizeGiven = true;
                        
                        /*Divide the amount specified by the size of the byte offste since it will 
                         * be multipled later*/
                        bkCdSt->bkCdBufSize = (atol(value) * getBufSizeMultiple(value)) / sizeof(oSetSt->byteOffset);
                    break;
                    case EXTR_FILE_BUFFER:
                        if (value == NULL) {
                            fprintf(stderr, "Missing value for suboption '%s'\n", token[EXTR_FILE_BUFFER]);
                            errflg = 1;
                            continue;
                        }
                        
                        if(!optSt->extractBytes) {
                            fprintf(stderr,"extracted_file_buffer will have no effect when mapping offsets\n");
                        }

                        optSt->extrFilBufSizeGiven = true;
                        extrFilSt->extrFilBufSize = atol(value) * sizeof(byte_t) * getBufSizeMultiple(value);
                    break;
                    default:
                        fprintf(stderr, "No match found for token: /%s/\n", value);
                        errflg = 1;
                    break;
                    }
                }
            }
        break;
        case 'v':
            if (optarg[0] == '-' && strlen(optarg) == 2) {
                optSt->verbosityLevel = 1;
            } else {
                optSt->verbosityLevel = atoi(optarg);
            }
        break;
        case 'h':
            printHelp(binName);
            exit(EXIT_SUCCESS);
        break;
        case 'd':
            optSt->allowDuplicates = true;
        break;
        case 'p':
            if(optSt->mapOffsets) {
                optSt->writeToStdout = true;
                optSt->outputFileGiven = true;
            }
            else if(optSt->extractBytes) {
                optSt->readFromStdin = true;
                optSt->bkCdGiven = true;
            }
        break;
        case 'r':
            optSt->resetAtEndOfBuf = true;
        break;
        case ':':
            fprintf(stderr, "Option -%c requires an argument\n", optopt);
            errflg++;
        break;
        case '?':
            errflg++;
        break;
        }
    }

    if(optSt->mapOffsets && optSt->extractBytes) {
        fprintf(stderr, "-m and -e are mutually exlusive. Can only map or extract, not both.\n");
        errflg++;
    }
    if(!optSt->mapOffsets && !optSt->extractBytes) {
        fprintf(stderr, "Must specify to either map or extract (-m or -e)\n");
        errflg++;
    }
    if( optSt->mapOffsets && !optSt->orgFilGiven) {
        fprintf(stderr, "Must specify an original file to use with -o\n");
        errflg++;
    }
    if(optSt->extractBytes && !optSt->bkCdGiven) {
        fprintf(stderr, "Must specify a book code file to use with -c\n");
        errflg++;
    }
    if (!optSt->bkFilGiven) {
        fprintf(stderr, "Must specify a bookfile to use with -b\n");
        errflg++;
    }
    if(!optSt->outputFileGiven) {
        fprintf(stderr, "Must specify an output file with -f\n");
        errflg++;
    }
    
    
    if (errflg) {
        printHelp(binName);
        exit(EXIT_FAILURE);
    }
}

size_t bytesOfRamAvailable(void) {
    FILE *meminfoFile = fopen("/proc/meminfo", "r");
    if(meminfoFile == NULL) {
        PRINT_FILE_ERROR("/proc/meminfo",errno);
        exit(EXIT_FAILURE);
    }

    char meminfoLine[256];
    while(fgets(meminfoLine, sizeof(meminfoLine), meminfoFile))
    {
        size_t availableMemory;
        if(sscanf(meminfoLine, "MemAvailable: %lu kB", &availableMemory) == 1)
        {
            fclose(meminfoFile);
            return availableMemory * 1024;
        }
    }

    fclose(meminfoFile);
    
    printf("Could not read available system memory\n");
    
    exit(EXIT_FAILURE);
    
}

size_t getFileSize(const char *filename)
{
    struct stat st;
    
    if(stat(filename, &st) == -1) {
        PRINT_SYS_ERROR(errno);
        exit(EXIT_FAILURE);
    }
    
    return st.st_size;
}

int main(int argc, char *argv[])
{
    
    if(argc == 1) {
        printHelp(argv[0]);
        exit(EXIT_FAILURE);
    }
     
    struct bookFileStruct bkFilSt;
    struct bookCodeStruct bkCdSt;
    struct originalFileStruct orgFilSt;
    struct extractedFileStruct extrFilSt;
    struct offsetStruct oSetSt;
    struct optionsStruct optSt = {0};
    
    parseOptions(argc, argv, &bkFilSt, &bkCdSt, &orgFilSt, &extrFilSt, &oSetSt, &optSt);

    bkFilSt.bkFil = NULL;
    bkCdSt.bkCd = NULL;
    orgFilSt.orgFil = NULL;
    extrFilSt.extrFil = NULL;

    bkFilSt.bkFilByte = 0;
    orgFilSt.orgFilByte = 0;

    bkCdSt.bkCdSize = 0;
    bkFilSt.bkFilSize = 0;
    orgFilSt.orgFilSize = 0;

    bkFilSt.bkFilPos = 0;
    bkFilSt.bkFilBufPos = 0;
    extrFilSt.extrFilBufPos = 0;

    oSetSt.byteOffset = 0;

    /* This digest will store offsets corresponding to byte values that have been mapped to a file 
     * from the original byte already in order to avoid consecutive repeats, though not necessarily 
     * duplicates. If a value is set  to -1 then a byte from the file could not be mapped. It must 
     * be initialized to -1 to allow for bytes mapped to offset 0.
     */
    for (int i = 0; i < 256; i++)
        oSetSt.offsetDigest[i] = -1;

    if (optSt.mapOffsets) {
        
        /*Open Files*/
        bkFilSt.bkFil = fopen(bkFilSt.bkFilName, "rb");
        if (bkFilSt.bkFil == NULL) {
            PRINT_FILE_ERROR(bkFilSt.bkFilName,errno);
            exit(EXIT_FAILURE);
        }

        if(optSt.writeToStdout) {
            bkCdSt.bkCd = stdout;
        } else {
            bkCdSt.bkCd = fopen(bkCdSt.bkCdFilName, "wb");
            if (bkCdSt.bkCd == NULL) {
                PRINT_FILE_ERROR(bkCdSt.bkCdFilName,errno);
                exit(EXIT_FAILURE);
            }
        }

        orgFilSt.orgFil = fopen(orgFilSt.orgFilName, "rb");
        if (orgFilSt.orgFil == NULL) {
            PRINT_FILE_ERROR(orgFilSt.orgFilName,errno);
            exit(EXIT_FAILURE);
        }

        /*Get File Sizes*/
        orgFilSt.orgFilSize = getFileSize(orgFilSt.orgFilName);        
        bkFilSt.bkFilSize = getFileSize(bkFilSt.bkFilName);
        
        /*Set buffer sizes*/
        if(!optSt.bkFilBufSizeGiven)
            bkFilSt.bkFilBufSize = DEFAULT_BUFFER_SIZE * sizeof(byte_t);
    
        if(!optSt.orgFilBufSizeGiven)
            orgFilSt.orgFilBufSize = DEFAULT_BUFFER_SIZE * sizeof(byte_t);
            
        /*Check buffer sizes against file sizes*/    
        if(bkFilSt.bkFilBufSize > bkFilSt.bkFilSize) {
            bkFilSt.bkFilBufSize = bkFilSt.bkFilBufSize;
        }
        
        if(orgFilSt.orgFilBufSize > orgFilSt.orgFilSize) {
            orgFilSt.orgFilBufSize = orgFilSt.orgFilSize;
        }
        
        if (bkFilSt.bkFilSize < bkFilSt.bkFilBufSize) {
                bkFilSt.bkFilBufSize = bkFilSt.bkFilSize;
        }
        
        if(optSt.verbosityLevel >= 1) {
            fprintf(stderr,"book_file_buffer %lu bytes\noriginal_file_buffer %lu bytes\n", (uint64_t)bkFilSt.bkFilBufSize, (uint64_t)orgFilSt.orgFilBufSize);
        }
        
        /*Check available memory*/
        if((orgFilSt.orgFilBufSize + bkFilSt.bkFilBufSize) > bytesOfRamAvailable()) {
            printf("Not enough available memory for specified buffer size\n");
            exit(EXIT_FAILURE);
        }
        
        /*Allocate buffers*/
        bkFilSt.bkFilBuffer = malloc(bkFilSt.bkFilBufSize);
        if (bkFilSt.bkFilBuffer == NULL) {
            PRINT_SYS_ERROR(errno);
            exit(EXIT_FAILURE);
        }
        
        orgFilSt.orgFilBuffer = malloc(orgFilSt.orgFilBufSize);
        if (orgFilSt.orgFilBuffer == NULL) {
            PRINT_SYS_ERROR(errno);
            exit(EXIT_FAILURE);
        }
        
        /*Set how much of bkFil to use*/
        
        /* bkFilSize needs to be an even multiple of the buffer size. This means the remainder 
         * of it won't be used but that is not a problem.
         */
        bkFilSt.bkFilSize -= bkFilSt.bkFilSize % bkFilSt.bkFilBufSize;

        if(optSt.verbosityLevel >= 1) {
            fprintf(stderr,"Mapping offsets...\n");
        }
        
        mapOffsets(&bkFilSt,&bkCdSt,&orgFilSt,&oSetSt,&optSt);
        
        fprintf(stderr,"Book code created\n");
        
        if(fclose(bkFilSt.bkFil) != 0) {
            PRINT_FILE_ERROR(bkFilSt.bkFilName,errno);
        }
        if(fclose(bkCdSt.bkCd) != 0) {
            PRINT_FILE_ERROR(bkCdSt.bkCdFilName,errno);
        }
        if(fclose(orgFilSt.orgFil) != 0) {
            PRINT_FILE_ERROR(orgFilSt.orgFilName,errno);
        }
        
        free(bkFilSt.bkFilBuffer);
        free(orgFilSt.orgFilBuffer);
        
        exit(EXIT_SUCCESS);

    } else if (optSt.extractBytes) {
        
        /*Open Files*/
        bkFilSt.bkFil = fopen(bkFilSt.bkFilName, "rb");
        if (bkFilSt.bkFil == NULL) {
            PRINT_FILE_ERROR(bkFilSt.bkFilName,errno);
            exit(EXIT_FAILURE);
        }

        if(optSt.readFromStdin) {
            bkCdSt.bkCd = stdin;
        } else {
            bkCdSt.bkCd = fopen(bkCdSt.bkCdFilName, "rb");
            if (bkCdSt.bkCd == NULL) {
                PRINT_FILE_ERROR(bkCdSt.bkCdFilName,errno);
                exit(EXIT_FAILURE);
            }
        }

        extrFilSt.extrFil = fopen(extrFilSt.extrFilName, "wb");
        if (extrFilSt.extrFil == NULL) {
            PRINT_FILE_ERROR(extrFilSt.extrFilName,errno);
            exit(EXIT_FAILURE);
        }

        /*Get file sizes*/
        bkCdSt.bkCdSize = getFileSize(bkFilSt.bkFilName);
        
        /*Set buffer sizes*/
        if(!optSt.extrFilBufSizeGiven)
            extrFilSt.extrFilBufSize = DEFAULT_BUFFER_SIZE * sizeof(byte_t);
    
        if(!optSt.bkCdBufSizeGiven)
            bkCdSt.bkCdBufSize = DEFAULT_BUFFER_SIZE * sizeof(byte_t);
        
        if(!optSt.readFromStdin) {    
            /*Check sizes between file sizes*/
            if(bkCdSt.bkCdBufSize > bkCdSt.bkCdSize) {
                bkCdSt.bkCdBufSize = bkCdSt.bkCdSize;
            }
            
            /* Multiply the size of the bookcode buffer since it will be holding uoffset_t sized offsets
             * that represent each byte of the original file we want to extract. This is important 
             * becase the loop in extractBytes makes each uoffset_t sizd chunk of the book code 
             * corespond to one byte of the extracted file buffer it indexes.
             */    
            bkCdSt.bkCdBufSize *= sizeof(oSetSt.byteOffset);
        }
        
        if(optSt.verbosityLevel >= 1) {
            fprintf(stderr,"extracted_file_buffer %lu bytes\nbook_code_buffer %lu bytes\n", (uint64_t)extrFilSt.extrFilBufSize, (uint64_t)bkCdSt.bkCdBufSize);
        }
        
        /*Check available memory*/
        if((bkCdSt.bkCdBufSize + extrFilSt.extrFilBufSize) > bytesOfRamAvailable()) {
            printf("Not enough available memory for specified buffer size\n");
            exit(EXIT_FAILURE);
        }
        
        /*Allocate buffers*/
        extrFilSt.extrFilBuffer = malloc(extrFilSt.extrFilBufSize);
        if (extrFilSt.extrFilBuffer == NULL) {
            PRINT_SYS_ERROR(errno);
            exit(EXIT_FAILURE);
        }
    
        bkCdSt.bkCdBuffer = malloc(bkCdSt.bkCdBufSize);
        if (bkCdSt.bkCdBuffer == NULL) {
            PRINT_SYS_ERROR(errno);
            exit(EXIT_FAILURE);
        }
        
        if(optSt.verbosityLevel >= 1) {
            fprintf(stderr,"Extracting bytes...\n");
        }
        extractBytes(&bkFilSt,&bkCdSt,&extrFilSt,&oSetSt,&optSt);

        fprintf(stderr,"Original file extracted from book code\n");
        
        if(fclose(bkFilSt.bkFil) != 0) {
            PRINT_FILE_ERROR(bkFilSt.bkFilName,errno);
        }
        if(fclose(bkCdSt.bkCd) != 0) {
            PRINT_FILE_ERROR(bkCdSt.bkCdFilName,errno);
        }
        if(fclose(extrFilSt.extrFil) != 0) {
            PRINT_FILE_ERROR(extrFilSt.extrFilName,errno);
        }
        
        free(extrFilSt.extrFilBuffer);
        free(bkCdSt.bkCdBuffer);
        
        exit(EXIT_SUCCESS);
    }

    exit(EXIT_FAILURE);
}
