#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "serial.h"

#define QUEUELIMIT 5

SERIAL *device;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
								'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
								'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
								'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
								'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
								'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
								'w', 'x', 'y', 'z', '0', '1', '2', '3',
								'4', '5', '6', '7', '8', '9', '+', '/'};
static char *decoding_table = NULL;
static int mod_table[] = {0, 2, 1};

void build_decoding_table()
{
	decoding_table = malloc(256);

	for (int i = 0; i < 64; i++)
		decoding_table[(unsigned char) encoding_table[i]] = i;
}

char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length)
{
	*output_length = 4 * ((input_length + 2) / 3);

	char *encoded_data = malloc(*output_length + 1);
	if (encoded_data == NULL) return NULL;

	for (int i = 0, j = 0; i < input_length;) {

		uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
		uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
		uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

		uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

		encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
	}

	for (int i = 0; i < mod_table[input_length % 3]; i++)
		encoded_data[*output_length - 1 - i] = '=';

	encoded_data[*output_length] = 0;

	return encoded_data;
}


unsigned char *base64_decode(const char *data, size_t input_length, size_t *output_length)
{
	if (decoding_table == NULL) build_decoding_table();

	if (input_length % 4 != 0) return NULL;

	*output_length = input_length / 4 * 3;
	if (data[input_length - 1] == '=') (*output_length)--;
	if (data[input_length - 2] == '=') (*output_length)--;

	unsigned char *decoded_data = malloc(*output_length);
	if (decoded_data == NULL) return NULL;

	for (int i = 0, j = 0; i < input_length;) {

		uint32_t sextet_a = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
		uint32_t sextet_b = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
		uint32_t sextet_c = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
		uint32_t sextet_d = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];

		uint32_t triple = (sextet_a << 3 * 6)
		+ (sextet_b << 2 * 6)
		+ (sextet_c << 1 * 6)
		+ (sextet_d << 0 * 6);

		if (j < *output_length) decoded_data[j++] = (triple >> 2 * 8) & 0xFF;
		if (j < *output_length) decoded_data[j++] = (triple >> 1 * 8) & 0xFF;
		if (j < *output_length) decoded_data[j++] = (triple >> 0 * 8) & 0xFF;
	}

	return decoded_data;
}

void base64_cleanup()
{
	free(decoding_table);
}

void checksum(void *payload, size_t siz)
{
	unsigned char *p = payload;
	
	unsigned char hadd = 0, hxor = 0;
	
	for(int i=0;i<siz;i++) {
		hadd += p[i];
		hxor ^= p[i];
	}
	
	printf("checksum: %02x %02x\n", hadd, hxor);
}

void *server_thread(void *port)
{
	int serv_sock;
	int clit_sock;
	struct sockaddr_in serv_addr;
	struct sockaddr_in clit_addr;
	unsigned short serv_port;
	unsigned int clit_len;
	
	if((serv_port = (unsigned short)atoi((char *)port)) == 0) {
		fprintf(stderr, "invalid port number.\n");
		return 0;
	}

	if((serv_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0 ) {
		fprintf(stderr, "socket() failed.");
		return 0;
	}

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(serv_port);
	
	int opt_yes = 1;
	
	setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt_yes, sizeof(opt_yes));
	
	if(bind(serv_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr) ) < 0 ) {
		fprintf(stderr, "bind() failed. (%s)");
		return 0;
	}

	if(listen(serv_sock, QUEUELIMIT) < 0) {
		fprintf(stderr, "listen() failed.");
		return 0;
	}

	while(1) {
		clit_len = sizeof(clit_addr);
		if ((clit_sock = accept(serv_sock, (struct sockaddr *) &clit_addr, &clit_len)) < 0) {
			fprintf(stderr, "accept() failed.");
			return 0;
		}

		printf("connected from %s.\n", inet_ntoa(clit_addr.sin_addr));
		
		unsigned char mode;
		recv(clit_sock, &mode, 1, MSG_WAITALL);
		
		if(mode == 'w') {
			char s[9];
			recv(clit_sock, s, 8, MSG_WAITALL);
			
			size_t siz;
			char *e;
			siz = strtol(s, &e, 16);
			
			char *payload = (char *)malloc(siz+1);
			memset(payload, 0, siz+1);
			recv(clit_sock, payload, siz, MSG_WAITALL);
			
			//pthread_mutex_lock(&mutex);
			//printf("RX %d bytes\n", siz);
			//puts(payload);
			//checksum(payload, siz);
			//pthread_mutex_unlock(&mutex);
			
			size_t siz2;
			
			unsigned char *payload2 = base64_decode(payload, siz, &siz2);
			
			serial_write(device, payload2, siz2);
			
			free(payload);
			free(payload2);
		}
		
		if(mode == 'r') {
			int t = 0;
			while(1) {
				usleep(5000);
				t++;
				if(t == 200) {
					send(clit_sock, "00000001", 8, 0);
					send(clit_sock, " ", 1, 0);
					t = 0;
				}
				
				if(serial_available(device) > 0) {
					size_t siz = serial_available(device);
					unsigned char *payload = (unsigned char *)malloc(siz);
					
					serial_read(device, payload, siz);
					
					size_t siz2;
					char *payload2 = base64_encode(payload, siz, &siz2);
					
					//pthread_mutex_lock(&mutex);
					//printf("TX %d bytes\n", siz2);
					//puts(payload2);
					//checksum(payload2, siz2);
					//pthread_mutex_unlock(&mutex);
					
					char s[9];
					sprintf(s, "%08x", siz2);
					
					send(clit_sock, s, 8, 0);
					send(clit_sock, payload2, siz2, 0);
					
					free(payload);
					free(payload2);
				}
			}
		}
		
		
		close(clit_sock);
	}

	return 0;
}

int main(int argc, char* argv[])
{
	if(argc < 2) return 0;
	
	pthread_t thread2011;
	pthread_t thread3647;
	
	pthread_mutex_init(&mutex, NULL);
	
	device = serial_open(argv[1], SerialBaud115200);
	
	pthread_create(&thread2011,NULL,server_thread,"2011");
	pthread_create(&thread3647,NULL,server_thread,"3647");
	
	while(1) {
		usleep(1000000);
	}
	
	pthread_detach(thread2011);
	pthread_detach(thread3647);
	
	return 0;
}
