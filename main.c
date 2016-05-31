#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <time.h>
#include <pthread.h>
#include <time.h>
#include <inttypes.h>
#include <stdlib.h>

#define BUFSIZE 32

char* ip = "unknown";
char* port = "unknown";
uint64_t counter = 0;

#define USE_FRAMEBUFFER 0

#if !USE_FRAMEBUFFER
	#define WIDTH   1024
	#define HEIGHT   768
#endif

struct timespec text_interval = {
	.tv_sec = 0,
	.tv_nsec = 300000000
};

struct timespec dump_interval = {
	.tv_sec = 0,
	.tv_nsec = 100000000
};

struct fbufinfo {
	uint32_t *data;
	int w, h;
	int bytespp;
	int fd;
};

void* print_info(void* unused) {
	// clear the screen
	fprintf(stderr, "\033[2J\033[1;1H");
	for(;;){
		fprintf(stderr, "\033[5;0HUDP Pixelflut\nnc -u %s %s\npixels/s: %6" PRIu64, ip, port, counter*1000000000/((uint64_t)text_interval.tv_nsec));
		counter = 0;
		nanosleep(&text_interval, NULL);
	}
	return NULL;
}

void* dump_fbuf(void* arg) {
	struct fbufinfo *info = (struct fbufinfo*)arg;

	for(;;){
		fwrite(info->data, info->bytespp * info->w * info->h, 1, stdout);
		nanosleep(&dump_interval, NULL);
	}
	return NULL;
}

uint32_t *mmap_framebuffer(char *device, int *w, int *h, int *bytespp, int *fd)
{
	int bitspp;
	uint32_t *data;

	// Öffnen des Gerätes
	*fd = open(device, O_RDWR);

	if(*fd == -1) {
		perror("Open failed");
		return NULL;
	}

	// Informationen über den Framebuffer einholen
	struct fb_var_screeninfo screeninfo;
	ioctl(*fd, FBIOGET_VSCREENINFO, &screeninfo);

	// Beende, wenn die Farbauflösung nicht 32 Bit pro Pixel entspricht
	bitspp = screeninfo.bits_per_pixel;
	if(bitspp != 32) {
		// Ausgabe der Fehlermeldung
		fprintf(stderr, "Farbaufloesung = %i Bits pro Pixel\n", bitspp);
		fprintf(stderr, "Bitte aendern Sie die Farbtiefe auf 32 Bits pro Pixel\n");
		close(*fd);
		return NULL;
	}

	fprintf(stderr, "Res: %ix%i, %i bpp\n", screeninfo.xres, screeninfo.yres, screeninfo.bits_per_pixel);

	*w       = screeninfo.xres;
	*h       = screeninfo.yres;
	*bytespp = bitspp/8; //Bytes pro Pixel berechnen

	// Zeiger auf den Framebufferspeicher anfordern
	data = (uint32_t*) mmap(0, (*w) * (*h) * (*bytespp), PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);

	return data;
}

int main(int argc, char **argv) {
	if(argc < 3){
		fprintf(stderr, "pass port and ip to be shown on command line");
		return 1;
	} else {
		ip = argv[1];
		port = argv[2];
	}
	int width, height, bytespp;
	int fd;
	uint32_t *data;

#if USE_FRAMEBUFFER
	data = mmap_framebuffer("/dev/fb0", &width, &height, &bytespp, &fd);
#else
	width = WIDTH;
	height = HEIGHT;
	bytespp = 4;
	data = malloc(width * height * bytespp);
	fd = -1;
#endif
	if(data == NULL) {
		fprintf(stderr, "Failed to allocate framebuffer.");
		return 1;
	}

	const int udpsock = socket(AF_INET6, SOCK_DGRAM, 0);
	struct sockaddr_in6 my_addr = {
		.sin6_family = AF_INET6,
		.sin6_port = htons(atoi(argv[2])),
		.sin6_addr = IN6ADDR_ANY_INIT,
	};
	if(0 != bind(udpsock, (struct sockaddr*)&my_addr, sizeof(my_addr))) {
		perror("Could not bind");
	}

	pthread_t print_info_thread;
	pthread_create(&print_info_thread, NULL, print_info, NULL);

	pthread_t print_dump_thread;
	struct fbufinfo fbinfo = {
		.data = data,
		.w = width,
		.h = height,
		.bytespp = bytespp,
		.fd = fd
	};

	pthread_create(&print_dump_thread, NULL, dump_fbuf, &fbinfo);

	char buf[BUFSIZE];
	for(;;){
		if(0 > recv(udpsock, (void*)buf, BUFSIZE, 0)) {
			perror("Error in receive");
		} else {
			unsigned int x,y,r,g,b,n;
			n = sscanf(buf, "PX %u %u %02x%02x%02x", &x, &y, &r, &g, &b);
			if (n == 5 && x<width && y<height) {
				data[y*width+x] = r<<16 | g<<8 | b;
			}
			++counter;
		}
	}

	// Zeiger wieder freigeben
#if USE_FRAMEBUFFER
	munmap(data, width * height * bytespp);

	// Gerät schließen
	close(fd);
#else
	free(data);
#endif

	// Rückgabewert
	return 0;
}
