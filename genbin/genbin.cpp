#include <stdio.h>
#include <stdint.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>

#define TARGET "htdocs.bin"
#define SOURCE_PATH "htdocs"

using namespace std;

typedef struct mime_pair {
    const char *ext;
    const char *mime;
    const char *encoding;
} mime_pair_t;

mime_pair_t mime_array[] = {
    {"css", "text/css", "gzip"},
    {"html", "text/html", "gzip"},
    {"htm", "text/html", "gzip"},
    {"js", "application/javascript", "gzip"},
    {"zip", "applicaiton/zip", ""},
    {"jpg", "image/jpeg", ""},
    {"jpeg", "image/jpeg", ""},
    {"jpe", "image/jpeg", ""},
    {"png", "image/png", ""},
    {"svg", "image/svg+xml", ""},
    {"ico", "image/x-icon", ""},
    {"gif", "image/gif", ""},
	{"wasm", "application/wasm", "gzip"},
    {"map", "application/json", "gzip"},
	{"mp3", "audio/mpeg", ""},
    {"webp", "image/webp", ""},
    {0, 0}
};

const char *get_extension(const char *filename)
{
    const char *d = strrchr(filename, (int)'.');
    if (d==0) {
        return "";
    }
    return d+1;
}

const char *get_mime_type(const char *filename)
{
    const char *ext = get_extension(filename);
    mime_pair_t *ptype = mime_array;
    if (strlen(ext)>0) {
        while (ptype->ext) {
            if (strcasecmp(ptype->ext, ext)==0) {
                return ptype->mime;
            }
            ptype++;
        }
    }
    return "application/octet-stream";
}

const char *get_content_encoding(const char *filename)
{
    const char *ext = get_extension(filename);
    mime_pair_t *ptype = mime_array;
    if (strlen(ext)>0) {
        while (ptype->ext) {
            if (strcasecmp(ptype->ext, ext)==0) {
                return ptype->encoding;
            }
            ptype++;
        }
    }
    return "";
}

#define align4(n)   (((n+3)>>2)<<2)


void read_file(const char *mypath, char **p, int32_t *size)
{
    const char *e = get_content_encoding(mypath);
    if (strcmp(e, "gzip") == 0) {
        char cmd[256];
        snprintf(cmd, 256, "gzip -c %s > output.bin.gz", mypath);
        if (system(cmd) == 0) {
            // donothin if success
        }
        mypath = "output.bin.gz";
    }
    FILE *infile = fopen(mypath, "rb");
    if (infile == NULL ) {
        printf("error in opening file (%s)\n", mypath);
        exit(1);
    }

    fseek(infile, 0, SEEK_END);
    *size = (int32_t)ftell(infile);
    fseek(infile, 0, SEEK_SET);

    *p = (char *)malloc((size_t)*size);
    if (*p == NULL) {
        printf("error in malloc memory size=%d\n", *size);
        exit(1);
    }

    if (fread(*p, 1, *size, infile) == (size_t) *size) {
        // donothing if success
    }
    fclose(infile);
}

void write_output(char *mypath, FILE *outfile)
{
    char *binpath = &mypath[strlen(SOURCE_PATH)];
    char *p;
    int32_t size;

    read_file(mypath, &p, &size);

    // write path
    int32_t plen = strlen(binpath) + 1;
    char *s = strstr(binpath, ".html");
    if (s==0) {
        fwrite(&plen, 1, sizeof(int32_t), outfile);
        fwrite(binpath, 1, align4(plen), outfile);
    } else {
        char buf[512];
        strcpy(buf, binpath);
        char *s = strstr(buf, ".html");
        *s = 0;
        plen = strlen(buf)+1;
        fwrite(&plen, 1, sizeof(int32_t), outfile);
        fwrite(buf, 1, align4(plen), outfile);
    }

    // write mime type
    const char *m = get_mime_type(mypath);
    int32_t mlen = strlen(m) + 1;

    fwrite(&mlen, 1, sizeof(int32_t), outfile);
    fwrite(m, 1, align4(mlen), outfile);

    // write content encoding
    const char *e = get_content_encoding(mypath);
    int32_t elen = strlen(e) + 1;
    fwrite(&elen, 1, sizeof(int32_t), outfile);
    fwrite(e, 1, align4(elen), outfile);

    // write data
    fwrite(&size, 1, sizeof(int32_t), outfile);
    fwrite(p, 1, align4(size), outfile);
    printf("    adding file: [%s] [%s] (%d)\n", binpath, m, size);

    free(p);
}

void _printdir(char *dir, FILE *outfile)
{
    DIR *dp;
    char mypath[512];
    struct dirent *entry;
    struct stat statbuf;

    if((dp = opendir(dir)) == NULL) {
        printf("cannot open directory: %s\n", dir);
        return;
    }
    while((entry = readdir(dp)) != NULL) {
        snprintf(mypath, sizeof(mypath), "%s/%s", dir, entry->d_name);
        lstat(mypath, &statbuf);
        if(S_ISDIR(statbuf.st_mode)) {
            if(strcmp(".", entry->d_name) == 0 ||
                strcmp("..", entry->d_name) == 0)
                continue;

            _printdir(mypath, outfile);
        } else {
            write_output(mypath, outfile);
        }
    }
    closedir(dp);
}

void printdir(char *dir)
{
    FILE *outfile = fopen(TARGET, "wb");
    if (outfile==NULL) {
        printf("cannot open file for write (%s)\n", TARGET);
        exit(0);
    }
    _printdir(dir, outfile);

    uint32_t endtag = 0;
    fwrite(&endtag, 1, sizeof(int32_t), outfile);
    fclose(outfile);
}

inline uint8_t *read_int32(uint8_t *p, int32_t *value)
{
    *value = *((int32_t *)p);
    return p + sizeof(int32_t);
}

#if 0
typedef struct web_path_t_ {
    uint32_t path_offset;
    uint32_t mime_offset;
    uint32_t data_offset;
    uint32_t data_size;
} web_path_t;

void gencode(uint8_t *start, vector<web_path_t> &c, FILE *outfile)
{
#if 0
    char *p = (char *)start;
    for (int i=0; i<c.size(); i++) {
        fprintf(outfile, "\nvoid fileserv_cb_%d(struct httpd_conn *conn)\n", i);
        fprintf(outfile, "{\n");
        fprintf(outfile, "    char *mime = (char *)&htdocs_bin[%d];\n", c[i].mime_offset);
        fprintf(outfile, "    uint8_t *data = (uint8_t *)&htdocs_bin[%d];\n", c[i].data_offset);
        fprintf(outfile, "    int data_size = %d;\n", c[i].data_size);
        fprintf(outfile, "    fileserve(conn, mime, data, data_size);\n");
        fprintf(outfile, "}\n\n");
    }

    fprintf(outfile, "void register_callbacks()\n{\n");
    for (int i=0; i<c.size(); i++) {
        char *path = p + c[i].path_offset;
        fprintf(outfile, "    httpd_reg_page_callback(\"%s\", fileserv_cb_%d);\n", path, i);
    }
    fprintf(outfile, "}\n\n");
#endif

	fprintf(outfile, "typedef struct web_path_s {\n");
	fprintf(outfile, "    const uint8_t *path;\n");
	fprintf(outfile, "    const uint8_t *mime;\n");
	fprintf(outfile, "    const uint8_t *data;\n");
	fprintf(outfile, "    uint32_t data_len;\n");
	fprintf(outfile, "} web_path_t;\n");
	fprintf(outfile, "\n");
	fprintf(outfile, "web_path_t web_path[] = {\n");
	for (int i=0; i<c.size(); i++) {
		fprintf(outfile, "    {&htdocs_bin[%d], &htdocs_bin[%d], &htdocs_bin[%d], %d},\n",
			c[i].path_offset, c[i].mime_offset, c[i].data_offset, c[i].data_size);
	}
	fprintf(outfile, "    {0, 0, 0, 0}\n");
	fprintf(outfile, "};\n");
}

void realize(uint8_t *p, vector<web_path_t> &c, FILE *outfile)
{
    uint8_t *start = p;

    for (;;) {
        uint8_t *path, *mime, *data;
        int32_t data_size;

        p = read_int32(p, &data_size);
        if (data_size == 0) 
            break;

        path = p;
        p += align4(data_size);

        p = read_int32(p, &data_size);
        mime = p;
        p += align4(data_size);

        p = read_int32(p, &data_size);
        data = p;
        p += align4(data_size);

        web_path_t web;
        web.path_offset = (uint32_t)(path - start);
        web.mime_offset = (uint32_t)(mime - start);
        web.data_offset = (uint32_t)(data - start);
        web.data_size = data_size;

        c.push_back(web);
    }
    gencode(start, c, outfile);
}

void deploy(const char *filename, const char *c_name)
{
    #define TARGET_C "htdocs_api.c"
    FILE *infile = fopen(filename, "rb");
    if (infile == NULL) {
        printf("cannot open [%s] for input\n", filename);
        exit(1);
    }

    fseek(infile, 0, SEEK_END);
    size_t size = ftell(infile);
    fseek(infile, 0, SEEK_SET);

    uint8_t *p = (uint8_t *)malloc(size);
    fread(p, 1, size, infile);
    fclose(infile);

    FILE *outfile = fopen(c_name, "wt");
    if (outfile == NULL) {
        printf("cannot open [%s] for output\n", c_name);
        exit(1);
    }

    vector<web_path_t> collection;
    realize(p, collection, outfile);

    fclose(outfile);
}
#endif

#define VER_NAME    "web_ver.html"
void read_version(int *major, int *minor)
{
    char buf[256];

    *major = 0;
    *minor = 0;
    snprintf(buf, 256, "%s/%s", SOURCE_PATH, VER_NAME);
    FILE *infile = fopen(buf, "rt");

    if (infile != NULL) {
        if (fscanf(infile, "%d.%d", major, minor) == 2) {
            // success, do nothing
        }
        fclose(infile);
    }
}

void write_version()
{
    char filename[256];
    int major, minor;

    read_version(&major, &minor);
    snprintf(filename, 256, "%s/%s", SOURCE_PATH, VER_NAME);
    FILE *outfile = fopen(filename, "wt");
    if (outfile == NULL) {
        printf("error: cannot create version.html\n");
        exit(1);
    }

    minor++;
    fprintf(outfile, "%d.%d", major, minor);

    fclose(outfile);
}

int main(int argc, char **argv)
{
    write_version();
    printdir((char *)SOURCE_PATH);
}
