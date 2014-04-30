#include <stdio.h>
#define BUFF_LEN 1024
int main (void)
{
	FILE *ifp, *ofp;
	char *inputFilename = "test_i.txt";
	char *outputFilename = "test_o.txt";
	char buffer[BUFF_LEN];
	unsigned checksum=0;
	int i, bytesRead;

	ifp = fopen("test_i.txt", "rb");
	ofp = fopen("test_o.txt", "wb");
	if (ifp == NULL) {
  		fprintf(stderr, "Can't open input file in.list!\n");
  		return(-1);
	}

	while (bytesRead = fread(buffer, sizeof(char), sizeof(buffer), ifp)) {
		for(i=0; i<bytesRead; i++) {
			checksum+= buffer[i];
		}	
		fwrite(buffer, sizeof(char), bytesRead, ofp);	
	}

	printf("IO checksum: %u\n", checksum);
	close(ifp);
	close(ofp);
	return 0;
}
