# bookcoder
A digital version of the bookcode cipher

With the classical book cipher, Alice maps each word of her message to a corresponding page and paragraph number where the word appears in a book. The list of numbers becomes the book code, and later Bob uses the numbers to look up the words in the book to extract Alice's message.

In this digital version, each byte of the original file is mapped to the offset in a book file where a corresponding byte resides, and the offset is then written to a file to become the book code. The program can then read the offsets from the book code, and seek to those positions in the book file and write the byte residing at that location to extract the original file.

With the original book cipher, Alice would use a book that is commonly and easily available, perhaps in a public library, and tell Bob which book was used and where to find it. Similarly, the digital version could use a file that is widely mirrored online to serve as the book file.

The offsets from the book file are written as 32-bit integers. Book files that are larger than 4 GB can still be used, since bytes can still be mapped to an offset within the 32-bit range. Unfortunately, this also means that for every byte of the original file, 4 bytes are stored making the book code 4 times as large as the original file. Fortunately, because most of the least-significant bits of the 32-bit integers will be null, most of those 4 bytes will also be null and heavily compressible.

If 32-bit integers are not sufficient to map offset sizes required, the code can be modified to change the uoffset_t and offset_t types to use 64-bit integers instead of 32-bit integers. This will of course result in book code files that are 8 times larger instead of 4. In my testing I have not found this necesary, so have restricted it to 32-bit integers.

# Details

For every byte of the original file, the book file is searched for a matching byte. If a matching byte is unable to be found, the position to search from in the book file is reset to the beginning, otherwise the position is progressed sequencially through the book file. Once a byte that matches the byte of the original file is found, the offset where this byte resides in the book file is written out to the book code. A digest of previously-used offsets is kept so that offsets are not repeated in the bookcode, which would make frequency analysis trivial. If a byte from the original file is unable to be mapped, or is only able to be mapped by repeating a previously used offset, an error informing the user that the book file did not have enough entropy is printed and the program exits.

Bytes are mapped in a buffered manner, with a default of 1 MB of bytes of the original file and the book file being stored and the comparisons made in memory. Buffering is needed because performing the comparison by merely reading the files in one byte at a time and using file functions to get the file offset reduce the speed that a file is able to be mapped at significantly. Buffered operation also allows for only a small portion of the book file to be used, resetting the position to the beginning of the file after the end of the buffer has been reached. This can help with producing a more compressible book code since more of the least-significant bits will be null if the offset range is kept to a smaller figure. On the other hand, some files may not have a suitable amount of entropy and the buffer size may need to be tweaked until it is large enough. The program can also be configured to allow repeats of previously-used offsets as a last resort.

To extract the original file from the book file using the book code, each offset in the book code is sought to, and the byte residing at that position is written out to reconstruct the original file. This also is done in a buffered manner to increase speed, but is still the slower of the operation since it relies on seeking to the offset in the book file. As with the buffers used to map the offsets, the default buffer size is 1 MB.

Since the book code file can only be made a practical size through compression, the program can map offsets and output the book code through standard output to a compression program. Likewise, a compression program can extract the bookcode from the compressed archive, and pipe that code through stanard input into the program.

The level of verbosity can be used to see how large the buffer sizes specified should be, see what portion of the files are being processed, and to observe what offsets have been read or written. For example, setting the verbosity level to 3 while mapping a file can be used to ensure that no offsets were duplicated.

# Platform

The majority of the code uses standard library and POSIX compatible functions, with the exception of options parsing which use the GNU long-options extension. This means it should work on basically any Linux or BSD system.

# Compilation

Optimization should be used or else the mapping speed will be very slow.
