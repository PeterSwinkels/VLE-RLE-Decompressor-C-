/*
VLE RLE Unpacker v1.00 - by: Peter Swinkels, ***2023***

This program decompresses files from the MS-DOS game Stunts/4D [Sports] Driving game by Distinctive Software Inc. v1.1 (1990)

 This program decompresses files with these extensions:
 *.cmn
 *.cod
 *.dif
 *.p3s
 *.pes
 *.pre
 *.pvs

This program is a rewrite. The original program's source code can be downloaded at:
https://github.com/dstien/gameformats/tree/master/stunts/stunpack/Source
*/

//This module contains this program's core procedures.

//This module's imports and settings.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//This structure defines a data buffer.
typedef struct {
	unsigned char* Data;		//Defines the data.
	unsigned int Position;	//Defines the position inside the data.
	unsigned int Length; 	//Defines the data's length.
} DataStr;

#define COMPRESSED_FILE_MAXIMUM_SIZE 0xFFFFFF       //Defines the maximum size a compressed file can be.
#define COMPRESSION_MULTIPLE_PASSES_FLAG 0x80       //Defines the flag indicating whether multiple compression passes are present.
#define COMPRESSION_PASS_COUNT_MASK 0x7F            //Defines the bits indicating the number of passes.
#define COMPRESSION_TYPE_RLE 0x1                    //Defines the run-length-encoding compression type.
#define COMPRESSION_TYPE_VLE 0x2                    //Defines the variable-length-encoding compression type.
#define FALSE 0;                                    //Defines the "false" boolean value.
#define RLE_ESCAPE_LENGTH_MASK 0x7F                 //Defines the bits indicating the number of escape codes.
#define RLE_ESCAPE_LENGTH_NO_SEQUENCE_RUN 0x80      //Defines the bit indicating a run is not a sequence run.
#define RLE_ESCAPE_LOOKUP_TABLE_LENGTH 0x100        //Defines the escape character lookup table size.
#define RLE_ESCAPE_MAXIMUM_LENGTH 0xA               //Defines the maximum number of escape codes.
#define RLE_SECOND_ESCAPE_CODE_POSITION 0x1         //Defines the zero-based position of the second escape code.
#define TRUE 1;                                     //Defines the "true" boolean value.
#define VLE_ALPHABET_LENGTH  0x100                  //Defines the number of alphabet codes.
#define VLE_BYTE_MSB_MASK 0x80                      //Defines the most significant bit in a byte.
#define VLE_ESCAPE_CHARACTERS_LENGTH 0x10           //Defines the number of escape codes.
#define VLE_ESCAPE_WIDTH 0x40                       //Defines the symbol width indicating the start of the escape sequence.
#define VLE_UNKNOWN_WIDTH_LENGTH 0x80               //Defines the flag that should not be set in the widths lengths value.
#define VLE_WIDTH_LENGTH_MASK 0x7F                  //Defines the mask for the widths lengths value.
#define VLE_WIDTH_MAXIMUM_LENGTH  0xF               //Defines the maximum widths lengths value.

//The function declarations.
int Decompress(DataStr* Source, DataStr* Target);
int GetSubFileSize(DataStr* Source);
int ReadCompressedFile(char* SourceFile, DataStr *Source);
int RLEDecodeSingleByteRun(DataStr* Source, DataStr* Target, int ByteRunLength, unsigned char ByteO);
int RLEDecodeSingleByteRuns(DataStr* Source, DataStr* Target, unsigned char* EscapeLookupTable);
int RLEDecompress(DataStr* Source, DataStr* Target);
int VLEDecompress(DataStr* Source, DataStr* Target);
int VLEGenerateEscapeTable(DataStr* Source, int* EscapeCharacters1, int* EscapeCharacters2, int WidthsLength);
void VLEGenerateLookupTable(DataStr* Source, int WidthsLengths, unsigned char* Alphabet, unsigned char* Symbols, unsigned char* Widths);
int WriteDecompressedFile(char* TargetFile, DataStr* Target);

//This procedure is executed when this program is started.
int main(int argc, char* argv[]) {
	DataStr Source = { NULL, 0, 0 };
	int Success = FALSE;
	DataStr Target = { NULL, 0, 0 };

	if (argc != 3) {
		printf("VLE RLE Unpacker v1.00 - by: Peter Swinkels, ***2023***\n");
		printf("\n");
		printf("Usage:\n");
		printf("\"VLE RLE Unpacker.exe\" SOURCE_FILE TARGET_FILE");
	}
	else {
		if (_stricmp(argv[1], argv[2]) == 0) {
			printf("The target file cannot be the same as the source file.\n");
		} else {
         Success = ReadCompressedFile(argv[1], &Source);
			if (Success) {
				Success = Decompress(&Source, &Target);
				if (Success) {
					printf("Decompressed \"%s\".\n", argv[1]);
					Success = WriteDecompressedFile(argv[2], &Target);
					if (Success) {
						printf("Wrote \"%s\".\n", argv[2]);
					} else {
						printf("Could not write \"%s\".\n", argv[2]);
					}
					free(Target.Data);
				} else {
					printf("Could not decompress \"%s\".\n", argv[1]);
				}
			} else {
				printf("Could not read \"%s\".\n", argv[1]);
			}
		}
	}

	return Success;
}

//This procedure decompresses the specified data.
int Decompress(DataStr* Source, DataStr* Target) {
	unsigned char CompressionType = 0;
	unsigned char PassCount = Source->Data[Source->Position];
	int Success = FALSE;

	if ((PassCount & COMPRESSION_MULTIPLE_PASSES_FLAG) == COMPRESSION_MULTIPLE_PASSES_FLAG) {
		PassCount = PassCount & COMPRESSION_PASS_COUNT_MASK;
		Source->Position += 4;
	}
	else {
		PassCount = 1;
	}

	if (Source->Position <= Source->Length) {
		for (int Pass = 0; Pass < PassCount; Pass++) {
			CompressionType = Source->Data[Source->Position];

			Source->Position += 1;
			if (Target->Data != NULL) free(Target->Data);
			Target->Length = GetSubFileSize(Source);
			Target->Data = malloc(sizeof(unsigned char) * Target->Length);
			if (Target->Data != NULL) {
				Success = FALSE;

				switch (CompressionType) {
					case COMPRESSION_TYPE_RLE:
						Success = RLEDecompress(Source, Target);
						break;
					case COMPRESSION_TYPE_VLE:
						Success = VLEDecompress(Source, Target);
						break;
				}

				if (Success && Pass < (PassCount - 1)) {
					free(Source->Data);
					Source->Position = 0;
					Source->Length = Target->Length;
					Source->Data = malloc(Source->Length);
					if (Source->Data == NULL) {
						break;
					}
					else {
						memcpy(Source->Data, Target->Data, Source->Length);
						Target->Position = 0;
					}
				}
				else {
					break;
				}
			}
		}
	}

	return Success;
}

//This procedure returns a compressed sub file's size from the specified source.
int GetSubFileSize(DataStr* Source) {
	int SubFileSize = Source->Data[Source->Position];

	SubFileSize = SubFileSize | (Source->Data[Source->Position + 1] << 0x8);
	SubFileSize = SubFileSize | (Source->Data[Source->Position + 2] << 0x10);
	Source->Position += 3;

	return SubFileSize;
}

//This procedure reads the specified compressed file.
int ReadCompressedFile(char* SourceFile, DataStr* Source) {
	int FileSize = 0;
	FILE* SourceFileO = NULL;
	int Success = FALSE;

	if (fopen_s(&SourceFileO, SourceFile, "rb") == 0) {
		fseek(SourceFileO, 0, SEEK_END);
		FileSize = ftell(SourceFileO);
		fseek(SourceFileO, 0, SEEK_SET);

		if (FileSize <= COMPRESSED_FILE_MAXIMUM_SIZE) {
			Source->Data = malloc(FileSize);
			if (Source->Data != NULL) {
				Source->Length = FileSize;
				if (fread(Source->Data, sizeof(unsigned char), Source->Length, SourceFileO) == FileSize) {
					Success = TRUE;
				} else {
					free(Source);
				}
			}
		}
		fclose(SourceFileO);
	}

	return Success;
}

//This procedure decodes RLE sequence runs inside the specified source and writes the result to the specified target.
int RLEDecodeSequenceRuns(DataStr* Source, DataStr* Target, const unsigned char SECOND_ESCAPE_CODE) {
	unsigned char CurrentByte = 0;
	int SequenceOffset = 0;
	int SequenceRunLength = 0;

	while (Source->Position < Source->Length) {
		CurrentByte = Source->Data[Source->Position];
		Source->Position += 1;

		if (CurrentByte == SECOND_ESCAPE_CODE) {
			SequenceOffset = Source->Position;

			CurrentByte = Source->Data[Source->Position];
			Source->Position += 1;
			while (CurrentByte != SECOND_ESCAPE_CODE) {
				if (Source->Position >= Source->Length) return FALSE;

				Target->Data[Target->Position] = CurrentByte;
				Target->Position += 1;

				CurrentByte = Source->Data[Source->Position];
				Source->Position += 1;
			}

			SequenceRunLength = Source->Data[Source->Position] - 1;
			Source->Position += 1;

			while (SequenceRunLength > 0) {
				SequenceRunLength -= 1;

				for (unsigned int Index = 0; Index < (Source->Position - SequenceOffset - 2); Index++) {
					if (Target->Position >= Target->Length) return FALSE;

					Target->Data[Target->Position] = Source->Data[SequenceOffset + Index];
					Target->Position += 1;
				}
			}
		}
		else {
			Target->Data[Target->Position] = CurrentByte;
			Target->Position += 1;

			if (Target->Position > Target->Length) return FALSE;
		}
	}

	return TRUE;
}

//This procedure decodes a single-byte run inside the specified source and writes the result to the specified target.
int RLEDecodeSingleByteRun(DataStr* Source, DataStr* Target, int ByteRunLength, unsigned char ByteO) {
	int Success = TRUE;

	while (ByteRunLength > 0) {
		ByteRunLength -= 1;
		if (Target->Position >= Target->Length) {
			Success = FALSE;
			break;
		}
		Target->Data[Target->Position] = ByteO;
		Target->Position += 1;
	}

	return Success;
}

//This procedure decodes single-byte runs inside the specified source and writes the result to the specified target.
int RLEDecodeSingleByteRuns(DataStr* Source, DataStr* Target, unsigned char* EscapeLookupTable) {
	int ByteRunLength = 0;
	unsigned char CurrentByte = 0;
	unsigned char EscapeCode = NULL;
	int Success = TRUE;

	while (Target->Position < Target->Length) {
		CurrentByte = Source->Data[Source->Position];
		Source->Position += 1;

		EscapeCode = EscapeLookupTable[CurrentByte];
		if (!(EscapeCode & 0xFF) == 0x0) {
			switch (EscapeCode) {
				case 1:
					ByteRunLength = Source->Data[Source->Position];
					Source->Position += 1;
					CurrentByte = Source->Data[Source->Position];
					Source->Position += 1;
					Success = RLEDecodeSingleByteRun(Source, Target, ByteRunLength, CurrentByte);
					break;
				case 3:
					ByteRunLength = Source->Data[Source->Position] | Source->Data[Source->Position + 1] << 8;
					Source->Position += 2;
					CurrentByte = Source->Data[Source->Position];
					Source->Position += 1;
					Success = RLEDecodeSingleByteRun(Source, Target, ByteRunLength, CurrentByte);
					break;
				default:
					ByteRunLength = EscapeLookupTable[CurrentByte] - 1;
					CurrentByte = Source->Data[Source->Position];
					Source->Position += 1;
					Success = RLEDecodeSingleByteRun(Source, Target, ByteRunLength, CurrentByte);
			}
		}
		else {
			Target->Data[Target->Position] = CurrentByte;
			Target->Position += 1;
		}
	}

	return Success;
}

//This procedure decompresses RLE data inside the specified source and writes the result to the specified target.
int RLEDecompress(DataStr* Source, DataStr* Target) {
	DataStr DecodedRLESequenceRunsTarget = { NULL, 0, 0 };
	unsigned char EscapeCodes[RLE_ESCAPE_MAXIMUM_LENGTH];
	unsigned char EscapeLength = 0;
	unsigned char EscapeLookupTable[RLE_ESCAPE_LOOKUP_TABLE_LENGTH];
	int Success = FALSE;

	memset(EscapeCodes, 0, RLE_ESCAPE_MAXIMUM_LENGTH);
	memset(EscapeLookupTable, 0, RLE_ESCAPE_LOOKUP_TABLE_LENGTH);

	Source->Position += 4;
	EscapeLength = Source->Data[Source->Position];

	Source->Position += 1;

	if ((EscapeLength & RLE_ESCAPE_LENGTH_MASK) > RLE_ESCAPE_MAXIMUM_LENGTH) return FALSE;

	for (int EscapeCodeIndex = 0; EscapeCodeIndex < (EscapeLength & RLE_ESCAPE_LENGTH_MASK); EscapeCodeIndex++) {
		EscapeCodes[EscapeCodeIndex] = Source->Data[Source->Position];
		Source->Position += 1;
	}

	if (Source->Position > Source->Length) return FALSE;

	for (int EscapeCodeIndex = 0; EscapeCodeIndex < (EscapeLength & RLE_ESCAPE_LENGTH_MASK); EscapeCodeIndex++) {
		EscapeLookupTable[EscapeCodes[EscapeCodeIndex]] = EscapeCodeIndex + 1;
	}

	if ((EscapeLength & RLE_ESCAPE_LENGTH_NO_SEQUENCE_RUN) == RLE_ESCAPE_LENGTH_NO_SEQUENCE_RUN) {
		Success = RLEDecodeSingleByteRuns(Source, Target, EscapeLookupTable);
	} else {
		DecodedRLESequenceRunsTarget.Length = Target->Length;
		DecodedRLESequenceRunsTarget.Position = Target->Position;
		DecodedRLESequenceRunsTarget.Data = malloc(sizeof(unsigned char) * DecodedRLESequenceRunsTarget.Length);

		if (DecodedRLESequenceRunsTarget.Data == NULL) return FALSE;

		if (!RLEDecodeSequenceRuns(Source, &DecodedRLESequenceRunsTarget, EscapeCodes[RLE_SECOND_ESCAPE_CODE_POSITION])) {
			free(DecodedRLESequenceRunsTarget.Data);
			Success = FALSE;
		} else {
			DecodedRLESequenceRunsTarget.Length = DecodedRLESequenceRunsTarget.Position;
			DecodedRLESequenceRunsTarget.Position = 0;

			Success = RLEDecodeSingleByteRuns(&DecodedRLESequenceRunsTarget, Target, EscapeLookupTable);
		}
	}

	return Success;
}

//This procedure decodes VLE compression codes inside the specified source and writes the result to the specified target.
int VLEDecode(DataStr* Source, DataStr* Target, unsigned char* Alphabet, unsigned char* Symbols, unsigned char* Widths, int* EscapeCharacters1, int* EscapeCharacters2) {
	unsigned char CurrentSymbol = 0;
	unsigned char CurrentWidth = 8;
	int CurrentWord = 0;
	int EscapeSequenceComplete = FALSE;
	unsigned char EscapeSequenceIndex = 0;
	int MSBBitSet = FALSE;
	unsigned char NextWidth = 0;

	CurrentWord = (Source->Data[Source->Position] << 8) & 0xFFFF;
	Source->Position += 1;
	CurrentWord = (CurrentWord | Source->Data[Source->Position]) & 0xFFFF;
	Source->Position += 1;

	while (Target->Position < Target->Length) {
		CurrentSymbol = (CurrentWord & 0xFF00) >> 8;
		NextWidth = Widths[CurrentSymbol];

		if (NextWidth > 8) {
			if (NextWidth != VLE_ESCAPE_WIDTH) return FALSE;

			CurrentSymbol = (CurrentWord & 0xFF);
			CurrentWord = (CurrentWord >> 8) & 0xFFFF;
			EscapeSequenceIndex = 7;
			EscapeSequenceComplete = FALSE;

			while (!EscapeSequenceComplete) {
				if (CurrentWidth == 0) {
					CurrentSymbol = Source->Data[Source->Position];
					Source->Position += 1;
					CurrentWidth = 8;
				}

				MSBBitSet = ((CurrentSymbol & VLE_BYTE_MSB_MASK) == VLE_BYTE_MSB_MASK);
				CurrentWord = ((CurrentWord << 1) | MSBBitSet) & 0xFFFF;
				CurrentSymbol <<= 1;
				CurrentWidth -= 1;
				EscapeSequenceIndex += 1;

				if (EscapeSequenceIndex >= VLE_ESCAPE_CHARACTERS_LENGTH) return FALSE;

				if (CurrentWord < EscapeCharacters2[EscapeSequenceIndex]) {
					CurrentWord = (CurrentWord + EscapeCharacters1[EscapeSequenceIndex]) & 0xFFFF;
					if (CurrentWord > 0xFF) return FALSE;
					Target->Data[Target->Position] = Alphabet[CurrentWord];
					Target->Position += 1;

					EscapeSequenceComplete = TRUE;
				}
			}

			CurrentWord = ((CurrentSymbol << CurrentWidth) | Source->Data[Source->Position] & 0xFFFF);
			Source->Position += 1;
			NextWidth = 8 - CurrentWidth;
			CurrentWidth = 8;
		}
		else {
			Target->Data[Target->Position] = Symbols[CurrentSymbol];
			Target->Position += 1;

			if (CurrentWidth < NextWidth) {
				CurrentWord = (CurrentWord << CurrentWidth) & 0xFFFF;
				NextWidth -= CurrentWidth;
				CurrentWidth = 8;
				if (Source->Position >= Source->Length) break;
				CurrentWord = (CurrentWord | Source->Data[Source->Position]) & 0xFFFF;
				Source->Position += 1;
			}
		}

		CurrentWord = (CurrentWord << NextWidth) & 0xFFFF;
		CurrentWidth -= NextWidth;

		if ((Source->Position - 1) > Source->Length && Target->Position < Target->Length) return FALSE;
	}

	return TRUE;
}

//This procedure decompresses VLE Data inside the specified source and writes the result to the specified target.
int VLEDecompress(DataStr* Source, DataStr* Target) {
	unsigned char Alphabet[VLE_ALPHABET_LENGTH];
	int AlphabetLength = 0;
	int CodesOffset = 0;
	int EscapeCharacters1[VLE_ESCAPE_CHARACTERS_LENGTH];
	int EscapeCharacters2[VLE_ESCAPE_CHARACTERS_LENGTH];
	int Success = FALSE;
	unsigned char Symbols[VLE_ALPHABET_LENGTH];
	unsigned char Widths[VLE_ALPHABET_LENGTH];
	unsigned char WidthsLengths = Source->Data[Source->Position];
	int WidthsOffset = 0;

	memset(Alphabet, 0, VLE_ALPHABET_LENGTH);

	Source->Position += 1;
	WidthsOffset = Source->Position;

	if (!((WidthsLengths & VLE_UNKNOWN_WIDTH_LENGTH) == VLE_UNKNOWN_WIDTH_LENGTH) || ((WidthsLengths & VLE_WIDTH_LENGTH_MASK) > VLE_WIDTH_MAXIMUM_LENGTH)) {
		AlphabetLength = VLEGenerateEscapeTable(Source, EscapeCharacters1, EscapeCharacters2, WidthsLengths);
		if (AlphabetLength <= VLE_ALPHABET_LENGTH) {
			for (int Letter = 0; Letter < AlphabetLength; Letter++) {
				Alphabet[Letter] = Source->Data[Source->Position];
				Source->Position += 1;
			}

			CodesOffset = Source->Position;
			Source->Position = WidthsOffset;

			VLEGenerateLookupTable(Source, WidthsLengths, Alphabet, Symbols, Widths);

			Source->Position = CodesOffset;

			Success = VLEDecode(Source, Target, Alphabet, Symbols, Widths, EscapeCharacters1, EscapeCharacters2);
		}
	}

	return Success;
}

//This procedure generates the VLE escape table and returns the VLE alphabet length.
int VLEGenerateEscapeTable(DataStr* Source, int* EscapeCharacters1, int* EscapeCharacters2, int WidthsLength) {
	int AlphabetLength = 0;
	unsigned char CurrentByte;
	int WidthSum = 0;

	for (int EscapeCharacter = 0; EscapeCharacter < WidthsLength; EscapeCharacter++) {
		WidthSum *= 2;
		EscapeCharacters1[EscapeCharacter] = AlphabetLength - WidthSum;
		CurrentByte = Source->Data[Source->Position];
		Source->Position += 1;
		WidthSum += CurrentByte;
		AlphabetLength += CurrentByte;
		EscapeCharacters2[EscapeCharacter] = WidthSum;
	}

	return AlphabetLength;
}

//This procedure generates the VLE look up table.
void VLEGenerateLookupTable(DataStr* Source, int WidthsLengths, unsigned char* Alphabet, unsigned char* Symbols, unsigned char* Widths) {
	int AlphabetIndex = 0;
	int SymbolWidthIndex = 0;
	unsigned char SymbolsPerWidth = VLE_BYTE_MSB_MASK;
	int WidthsDistributionLengths = (WidthsLengths >= 8 ? 8 : WidthsLengths);

	for (int Width = 1; Width <= WidthsDistributionLengths; Width++) {
		for (unsigned char SymbolWidth = Source->Data[Source->Position]; SymbolWidth > 0; SymbolWidth--) {
			for (unsigned char SymbolsRemaining = SymbolsPerWidth; SymbolsRemaining > 0; SymbolsRemaining--) {
				Symbols[SymbolWidthIndex] = Alphabet[AlphabetIndex];
				Widths[SymbolWidthIndex] = Width;
				SymbolWidthIndex += 1;
			}
			AlphabetIndex += 1;
		}
		Source->Position += 1;
		SymbolsPerWidth >>= 1;
	}

	for (int RemainingWidthsIndex = SymbolWidthIndex; RemainingWidthsIndex < VLE_ALPHABET_LENGTH; RemainingWidthsIndex++) {
		Widths[RemainingWidthsIndex] = VLE_ESCAPE_WIDTH;
	}
}

//This procedure writes the specified data to the specified file.
int WriteDecompressedFile(char* TargetFile, DataStr* Target) {
	int Success = FALSE;
	FILE* TargetFileO = NULL;

	if (fopen_s(&TargetFileO, TargetFile, "wb") == 0) {
		fwrite(Target->Data, sizeof(unsigned char), Target->Length, TargetFileO);
		fclose(TargetFileO);
		Success = TRUE;
	}

	return Success;
}

