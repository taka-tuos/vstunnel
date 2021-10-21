#include <curl/curl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <locale.h>
#include <unistd.h>
#include <pthread.h>

#include "serial.h"

#define CURL_USERAGENT "curl/" LIBCURL_VERSION

char *url_prefix = NULL;

char *tx_url = NULL;
char *rx_url = NULL;

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

void send_data(unsigned char *payload, size_t siz)
{
	CURLcode ret;
	CURL *hnd;
	struct curl_slist *slist1;

	slist1 = NULL;
	slist1 = curl_slist_append(slist1, "Content-Type: application/json");

	size_t siz2;
	char *payload2;

	//printf("TX encoding..");
	payload2 = base64_encode(payload, siz, &siz2);
	//printf("done\n");

	//pthread_mutex_lock(&mutex);
	//printf("TX %d bytes\n", siz2);
	//puts(payload2);
	//checksum(payload2, siz2);
	//pthread_mutex_unlock(&mutex);

	FILE *f = fopen("/dev/null", "wb");

	hnd = curl_easy_init();
	curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 102400L);
	curl_easy_setopt(hnd, CURLOPT_URL, tx_url);
	curl_easy_setopt(hnd, CURLOPT_POSTFIELDS, payload2);
	curl_easy_setopt(hnd, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)siz2);
	curl_easy_setopt(hnd, CURLOPT_HTTPHEADER, slist1);
	curl_easy_setopt(hnd, CURLOPT_USERAGENT, CURL_USERAGENT);
	curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
	curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
	curl_easy_setopt(hnd, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(hnd, CURLOPT_FTP_SKIP_PASV_IP, 1L);
	curl_easy_setopt(hnd, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(hnd, CURLOPT_WRITEDATA, f);
	
	//printf("TX sending..");
	ret = curl_easy_perform(hnd);
	//printf("done\n");
	
	fclose(f);

	curl_easy_cleanup(hnd);
	hnd = NULL;
	curl_slist_free_all(slist1);
	slist1 = NULL;
}

char *rx_streaming_payload = NULL;

void rx_streaming_received(void)
{
	char *p = rx_streaming_payload;
	for(;*p!=' ';p++);
	*p = 0;
	
	size_t siz = strlen(rx_streaming_payload);
	char *payload = rx_streaming_payload;
	
	//pthread_mutex_lock(&mutex);
	//printf("RX %d bytes\n", siz);
	//puts(payload);
	//checksum(payload, siz);
	//pthread_mutex_unlock(&mutex);
	
	size_t siz2;
	unsigned char *payload2;
	
	//printf("RX decoding..");
	payload2 = base64_decode(payload, siz, &siz2);
	//printf("done\n");
	
	//printf("RX sending..");
	serial_write(device, payload2, siz2);
	//printf("done\n");
	
	free(rx_streaming_payload);
	free(payload2);
	rx_streaming_payload = NULL;
}

size_t rx_streaming_callback(void* ptr, size_t size, size_t nmemb, void* data) {
	if (size * nmemb == 0)
		return 0;
	
	char **payload = ((char **)data);
	
	size_t realsize = size * nmemb;
	
	size_t length = realsize + 1;
	char *str = *payload;
	str = realloc(str, (str ? strlen(str) : 0) + length);
	if(*((char **)data) == NULL) strcpy(str, "");
	
	*payload = str;
	
	if (str != NULL) {
		strncat(str, ptr, realsize);
		if(str[strlen(str)-1] == 0x0a) {
			if(*str == ' ') {
				free(str);
				*payload = NULL;
			} else {
				rx_streaming_received();
			}
		}
	}

	return realsize;
}

void *rx_stream_thread_func(void *param)
{
	CURLcode ret;
	CURL *hnd;
	char errbuf[CURL_ERROR_SIZE];

	while(1) {
		hnd = curl_easy_init();
		curl_easy_setopt(hnd, CURLOPT_URL, rx_url);
		curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 1L);
		curl_easy_setopt(hnd, CURLOPT_USERAGENT, CURL_USERAGENT);
		curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
		curl_easy_setopt(hnd, CURLOPT_CUSTOMREQUEST, "GET");
		curl_easy_setopt(hnd, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(hnd, CURLOPT_TIMEOUT, 0);
		curl_easy_setopt(hnd, CURLOPT_WRITEDATA, (void *)&rx_streaming_payload);
		curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, rx_streaming_callback);
		curl_easy_setopt(hnd, CURLOPT_ERRORBUFFER, errbuf);

		ret = curl_easy_perform(hnd);

		curl_easy_cleanup(hnd);
		hnd = NULL;
	}
	
	return NULL;
}

void *tx_stream_thread_func(void *param)
{
	while(1) {
		if(serial_available(device) > 0) {
			size_t siz = serial_available(device);
			void *payload = malloc(siz);
			
			serial_read(device, payload, siz);
			
			send_data(payload,siz);
		}
	}
	
	return NULL;
}

int main(int argc, char *argv[])
{
	if(argc < 3) return 0;
	
	device = serial_open(argv[1], SerialBaud115200);
	
	url_prefix = strdup(argv[2]);
	
	tx_url = strdup(argv[2]);
	rx_url = strdup(argv[2]);
	
	strcat(tx_url,"tx.php");
	strcat(rx_url,"rx.php");
	
	pthread_t rx_thread;
	pthread_t tx_thread;
	
	pthread_mutex_init(&mutex, NULL);
	
	pthread_create(&rx_thread, NULL, rx_stream_thread_func, NULL);
	pthread_create(&tx_thread, NULL, tx_stream_thread_func, NULL);
	
	while(1) {
		usleep(1000000);
	}
	
	pthread_detach(rx_thread);
	pthread_detach(tx_thread);
	
	return 0;
}
