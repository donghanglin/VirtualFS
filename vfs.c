/*
  compile:
  $ gcc -Wall vfs.c `pkg-config fuse --cflags --libs` -o vfs

  need root privilege to mount this file system:
  $ ./vfs /tmp/fuse

  supported linux commands:
  $ touch
  $ mkdir
  $ mv
  $ echo
  $ cat
  $ ln
  $ rm
  $ rm -r
  $ df
  $ cp
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

// limitation of this virtual file system
#define MAX_BLOCK_NUM 10000
#define MAX_INODE_NUM 2000
#define BLOCK_SIZE 4096
#define MAX_FILE_BLOCK 400
#define MAX_FILE_NUM 50
#define MAX_PATH_LEN 1000
#define MAX_NAME_LEN 50

static const char *fusedata = "/fusedata/fusedata.";

static struct superblock {
	int creationTime;
	int mounted;
	int devId;
	int freeStart;
	int freeEnd;
	int root;
	int maxBlocks;
	int freeblocks;
	int freeinodes;
}Superblock;

struct file_to_inode_dict {
	char type;	
	char name[MAX_NAME_LEN];
	int inode;
};

static struct inode {
	int size;
	int uid;
	int gid;
	int mode;
	int atime;
	int ctime;
	int mtime;
	int linkcount;
	int subn;
	int indirect;
	int location;
	char type;
	struct file_to_inode_dict filename_to_inode_dict[MAX_FILE_NUM];
}block[MAX_BLOCK_NUM];

static int freeblock[25][400];
static char zero[BLOCK_SIZE];

void initial_freeblock(void);
int split_to_blockn(const char *path, int parent);
int find_parent_inode(const char *path);
char* split_to_name(const char *path);
int same_name_in_path(const char *path);
int find_first_freeblock(void);
int find_name_in_inode(struct inode p, char *name);
void write_freeblock(int idxi);
void restore_freeblock(int idxn);
void write_dir_inode(struct inode ino);
void write_file_inode(struct inode ino, int blockn);
void empty_file(int filelocation);
void remove_file(int filelocation);
int blocklist(char* list, int mode);
void write_file(int filelocation, char* content, int from, int to);

void initial_freeblock(void) 
{
	int i,j;
	for (j = 0; j < 400; j++) {
		if (j <= 25) 
		freeblock[0][j] = 0;
		else
		freeblock[0][j] = j;
	}
	for (i = 1; i < 26; i++) {
		for (j = 0; j < 400; j++) {
			freeblock[i][j] = 400 * i + j;
		}
	}

	freeblock[0][26] = 0;

	for (i = 1; i < 26; i++) {
		write_freeblock(i);
	}
}

int split_to_blockn(const char *path, int parent) 
{	
	// parent == 1: find parent path inode
	// parent == 0: find path inode

	int i, j = 0, N;
	char *name[MAX_FILE_NUM];
	char temp[MAX_PATH_LEN];
	strcpy(temp, path);
	int temi[MAX_PATH_LEN];

	for (i = 0; i < strlen(path); i++) {
		if (temp[i] == '/') {
			temp[i] = '\0';
			temi[j++] = i;  
		}
	}
	
	N = j;
	for (i = 0; i < N - parent; i++) {
		name[i] = temp + temi[i] + 1;
	}
	
	int inoden = 26;
	for (i = 0; i < N - parent; i++) {
		for (j = 0; j < MAX_FILE_NUM; j++) {
			if (strcmp(block[inoden].filename_to_inode_dict[j].name, name[i]) == 0) {
				inoden = block[inoden].filename_to_inode_dict[j].inode;
				break;				
			}
		}
	}

	return inoden;
}

int find_parent_inode(const char *path)
{
	char temp[MAX_PATH_LEN];
	int i, pathcount = 0;
	int parent_inode;
	strcpy(temp, path);

	for (i = 0; i < strlen(path); i++) {
		if (temp[i] == '/') {
			pathcount++; 
		}
	}

	if (pathcount == 1) {
		return 26;
	}
	else {
		parent_inode = split_to_blockn(path, 1);
		return parent_inode;
	}
}

char* split_to_name(const char *path) 
{	
	// split path to get file name

	int i,j=0,N;	
	char *name[MAX_FILE_NUM];
	char temp[MAX_PATH_LEN];
	char *tem;
	strcpy(temp, path);
	int temi[MAX_PATH_LEN];

	for (i = 0; i < strlen(path); i++) {
		if (temp[i] == '/') {
			temp[i] = '\0';
			temi[j++] = i;  
		}
	}
	
	N = j;
	for (i = 0; i < N; i++) {
		name[i] = temp + temi[i] + 1;
	}
	
	tem = name[--i];
	return tem;
}

int find_name_in_inode(struct inode p, char *name)
{
	int i, hit = 0;
	for (i = 0; i < p.subn; i++) {
		if (strcmp(p.filename_to_inode_dict[i].name, name) == 0) {
			hit = i;
			return hit;
		}
	}
	return hit;
}

int find_first_freeblock(void)
{
	int i, j, hit = 0, first_freeblock = -1;
	for (i = 0; i < 25; i++) {
		for (j = 0; j < 400; j++){
			if (freeblock[i][j] != 0) {
				first_freeblock = freeblock[i][j];
				freeblock[i][j] = 0;
				hit = 1;
				write_freeblock(i+1);
				break;
			}
		}
		if (hit == 1) {
			break;
		}
	}
	if (first_freeblock != 1) {
		Superblock.freeblocks--;
	}
	return first_freeblock;
}

void write_dir_inode(struct inode ino) 
{
	char filename[30];
	int i;

	// get inode number
	int ino_num = ino.filename_to_inode_dict[0].inode;
	
	FILE *fp;
	sprintf(filename, "%s%d", fusedata, ino_num);
	fp = fopen(filename, "r+");
	
	fprintf(fp, "{size:%d, uid:%d, gid:%d, mode:%d, atime:%d, ctime:%d, mtime:%d, linkcount:%d, ", 
		    ino.size, ino.uid, ino.gid, ino.mode, ino.atime, ino.ctime, ino.mtime, ino.linkcount);
	
	fputs("filename_to_inode_dict: {", fp);
	for (i = 0; i < ino.subn; i++) {
		fprintf(fp, "%c:%s:%d", ino.filename_to_inode_dict[i].type, ino.filename_to_inode_dict[i].name,
							    ino.filename_to_inode_dict[i].inode);
		if (i < ino.subn - 1) {
			fputs(", ", fp);
		}
	}
	fputs("}}", fp);
	fputs("00000000000000000000000000000", fp);
	fclose(fp);
}

void write_file_inode(struct inode ino, int blockn) 
{
	char filename[30];
	int ino_num = blockn;
	
	FILE *fp;
	sprintf(filename, "%s%d", fusedata, ino_num);
	fp = fopen(filename, "r+");
	
	fprintf(fp, "{size:%d, uid:%d, gid:%d, mode:%d, linkcount:%d, atime:%d, ctime:%d, mtime:%d, indirect:%d, location:%d", 
		    ino.size, ino.uid, ino.gid, ino.mode, ino.linkcount, ino.atime, ino.ctime, ino.mtime, ino.indirect, ino.location);
	
	fputs("}", fp);
	fputs("00000000000000000000000000000", fp);
	fclose(fp);
}

void write_freeblock(int idxi)
{
	char filename[30];
	int j;
	
	FILE *fp;
	sprintf(filename, "%s%d", fusedata, idxi);
	fp = fopen(filename, "w");
	
	idxi = idxi - 1;
	for (j = 0; j < 400; j++) {
		if (idxi != 0 || j > 25) {
			fprintf(fp, "%d, ", freeblock[idxi][j]);
		}
	}
	fclose(fp);
}

void restore_freeblock(int idxn)
{
	int i = idxn / 400;
	int j = idxn % 400;
	freeblock[i][j] = idxn;
	write_freeblock(i+1);

	char filename[30];
	sprintf(filename, "%s%d", fusedata, idxn);
	FILE *fp;
	fp = fopen(filename, "w");
	fprintf(fp, "%s", zero);
	fclose(fp);

	Superblock.freeblocks++;
}

void empty_file(int filelocation)
{
	char filename[30];
	sprintf(filename, "%s%d", fusedata, filelocation);
	FILE *fp;
	fp = fopen(filename, "w");
	fclose(fp);
}

void remove_file(int filelocation)
{
	int i, r;
	if(block[filelocation].indirect == 1) {
			char filename[30];
			sprintf(filename, "%s%d", fusedata, block[filelocation].location);
			FILE *fp;
			char blocklist_info[1700];
			for (i = 0; i < 1700; i++) {
				blocklist_info[i] = '\0';
			}
			fp = fopen(filename, "r");
			r = fread(blocklist_info, 1, 1700, fp);
			fclose(fp);
			r = blocklist(blocklist_info, 3);	
	}

	restore_freeblock(block[filelocation].location);
	restore_freeblock(filelocation);
	memset(&block[filelocation], 0, sizeof(block[filelocation]));

	(void) r;
}

void write_file(int filelocation, char* content, int from, int to)
{
	int i;
	char filename[30];
	sprintf(filename, "%s%d", fusedata, filelocation);
	FILE *fp;
	fp = fopen(filename, "w");
	for (i = from; i <= to; i++) {
		fprintf(fp, "%c", content[i]);
	}
	fclose(fp);
}

int blocklist(char* list, int mode)
{
	// mode == 1: return last block in filedata blocklist
	// mode == 2: return block num taken
	// mode == 3: loopup blocklist and empty
	
	int i, k;
	char blocklist[4096];
	char block[5];
	int blockn[MAX_FILE_BLOCK];
	strcpy(blocklist, list);
	int j = 0, n = 0;
	for (i = 0; i < strlen(blocklist); i++) {
		if (blocklist[i] != ',') {
			block[j++] = blocklist[i];
		}

		else {
			blockn[n++] = atoi(block);
			for (k = 0; k < 5; k++) {
				block[k] = '\0';
			}
			j = 0;
			if (mode == 3) {
				restore_freeblock(blockn[n - 1]);
			}
		}
	}
	if (mode == 1) {
		return blockn[--n];
	}
	else if (mode == 2) {
		return --n;
	}
	else {
		return 0;
	}
}

static int vfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{	
	int firstblock, fileblock;
	firstblock = find_first_freeblock();
	fileblock = find_first_freeblock();
	if (firstblock == -1 || fileblock == -1) {
		return -ENOSPC;
	}
	else {
		Superblock.freeinodes -= 2;
	}

	mode = S_IFREG | 0664;

	block[firstblock].size = 0;
	block[firstblock].uid = 1;
	block[firstblock].gid = 1;
	block[firstblock].mode = mode;
	block[firstblock].atime = (int) time(NULL);
	block[firstblock].ctime = (int) time(NULL);
	block[firstblock].mtime = (int) time(NULL);
	block[firstblock].linkcount = 1;
	block[firstblock].subn = 0;
	block[firstblock].indirect = 0;
	block[firstblock].location = fileblock;

	write_file_inode(block[firstblock], firstblock);

	// modify parent inode
	int parent_inode = find_parent_inode(path);
	
	char *mkdirname = split_to_name(path);
	strcpy(block[parent_inode].filename_to_inode_dict[block[parent_inode].subn].name, mkdirname);
	block[parent_inode].filename_to_inode_dict[block[parent_inode].subn].type = 'f';
	block[parent_inode].filename_to_inode_dict[block[parent_inode].subn].inode = firstblock;
	block[parent_inode].subn++;
	
	write_dir_inode(block[parent_inode]);

	char filename[30];
	FILE *fp;
	sprintf(filename, "%s%d", fusedata, block[firstblock].location);
	fp = fopen(filename, "w");
	fclose(fp);

	(void) fi;
	return 0;	            
}

static int vfs_mkdir(const char *path, mode_t mode)
{
	int firstblock;
	firstblock = find_first_freeblock();
	if (firstblock == -1) {
		return -ENOSPC;
	}
	else {
		Superblock.freeinodes--;
	}

	mode = 16877;

	block[firstblock].size = 4096;
	block[firstblock].uid = 1;
	block[firstblock].gid = 1;
	block[firstblock].mode = mode;
	block[firstblock].atime = (int) time(NULL);
	block[firstblock].ctime = (int) time(NULL);
	block[firstblock].mtime = (int) time(NULL);
	block[firstblock].linkcount = 2;
	block[firstblock].subn = 2;
	strcpy(block[firstblock].filename_to_inode_dict[0].name, ".");
	block[firstblock].filename_to_inode_dict[0].type = 'd';
	block[firstblock].filename_to_inode_dict[0].inode = firstblock;	
	strcpy(block[firstblock].filename_to_inode_dict[1].name, "..");
	block[firstblock].filename_to_inode_dict[1].type = 'd';
	block[firstblock].filename_to_inode_dict[1].inode = find_parent_inode(path); 

	write_dir_inode(block[firstblock]);

	// modify parent inode
	int parent_inode = find_parent_inode(path);
	
	char *mkdirname = split_to_name(path);
	strcpy(block[parent_inode].filename_to_inode_dict[block[parent_inode].subn].name, mkdirname);
	block[parent_inode].filename_to_inode_dict[block[parent_inode].subn].type = 'd';
	block[parent_inode].filename_to_inode_dict[block[parent_inode].subn].inode = firstblock;
	block[parent_inode].subn++;
	block[parent_inode].linkcount++;

	write_dir_inode(block[parent_inode]);

	return 0;
}

static int vfs_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	
	int block_num, parent_inode, i, hit = 0;
	char name[MAX_NAME_LEN];
	char* splitname = split_to_name(path);
	struct inode p;
	memset(stbuf, 0, sizeof(struct stat));

	splitname = split_to_name(path);
	strcpy(name, splitname);

	if (strcmp(path, "/") == 0) {
		p = block[26];
	} 
	else {	
		parent_inode = find_parent_inode(path);
		p = block[parent_inode];
		for (i = 0; i < p.subn; i++) {
			
			if (strcmp(p.filename_to_inode_dict[i].name, name) == 0) {
				block_num = p.filename_to_inode_dict[i].inode;
				p = block[block_num];
				hit = 1;
				break;
			}			
		}
		if (hit == 0){
			return -ENOENT;
		}
	}
	
	stbuf->st_mode = p.mode;
	stbuf->st_nlink = p.linkcount;
	stbuf->st_size = p.size;
	stbuf->st_atime = p.atime;
	stbuf->st_ctime = p.ctime;
	stbuf->st_mtime = p.mtime;

	return res;	
}

static int vfs_opendir(const char *path, struct fuse_file_info *fi)
{
	(void) fi;
	if (strcmp(path, "/") == 0) {
		return 0;
	}
	int parent_inode = find_parent_inode(path);
	char* parent_name = split_to_name(path);
	int isInparent = find_name_in_inode(block[parent_inode], parent_name);

	if (isInparent == 0) {
		return -ENOENT;
	}

	return 0;
}

static int vfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;
	struct inode p;
	int i, block_num;
	if (strcmp(path, "/") == 0) {
		p = block[26];	
	}
	else {
		block_num = split_to_blockn(path, 0);
		p = block[block_num];
	}

	for (i = 0; i < p.subn; i++) {
		filler(buf, p.filename_to_inode_dict[i].name, NULL, 0);
	}
	
	return 0;
}

static int vfs_releasedir(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
	return 0;
}

static int vfs_open(const char *path, struct fuse_file_info *fi)
{
	(void) fi;
	int parent_inode = find_parent_inode(path);
	char* parent_name = split_to_name(path);
	int isInparent = find_name_in_inode(block[parent_inode], parent_name);

	if (isInparent == 0) {
		return -ENOENT;
	}

	return 0;
}

static int vfs_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
	return 0;
}

static int vfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	int i, r;
	char filename[30];
	int inoden = split_to_blockn(path, 0);
	int datablockn = block[inoden].location;

	if (block[inoden].indirect == 0) {
		sprintf(filename, "%s%d", fusedata, datablockn);
		FILE *fp;
		fp = fopen(filename, "r");
		r = fread(buf, 1, 4096, fp);
		fclose(fp);
	}
	else {
		char blocklist_info[1700];
		for (i = 0; i < 1700; i++) {
			blocklist_info[i] = '\0';
		}
		sprintf(filename, "%s%d", fusedata, datablockn);
		FILE *fp;
		fp = fopen(filename, "r");
		r = fread(blocklist_info, 1, 1700, fp);
		fclose(fp);

		int i, k;
		char buff[4096];
		char blocklist[1700];
		char block[5];
		int blockn[MAX_FILE_BLOCK];
		strcpy(blocklist, blocklist_info);
		int j = 0, n = 0;
		for (i = 0; i < strlen(blocklist); i++) {
			if (blocklist[i] != ',') {
				block[j++] = blocklist[i];
			}
			else {
				blockn[n++] = atoi(block);
				for (k = 0; k < 5; k++) {
					block[k] = '\0';
				}
				j = 0;
			}
		}
		for (i = 0; i < n; i++) {
			sprintf(filename, "%s%d", fusedata, blockn[i]);
			FILE *fp;
			fp = fopen(filename, "r");
			r = fread(buff, 1, 4096, fp);
			strcat(buf, buff);
			fclose(fp);
		}
	}

	(void) fi;
	(void) r;
	(void) offset;
	return size;
}

static int vfs_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
	int i, r;
	char filename[30];
	int newblock[MAX_FILE_BLOCK];
	int firstblock, lastfileblockn, blocktaken;
	int inoden = split_to_blockn(path, 0);

	if (block[inoden].indirect == 0) {
		lastfileblockn = block[inoden].location;
		blocktaken = 1;
	}
	else {
		int idxblockn = block[inoden].location;
		FILE *fp;
		sprintf(filename, "%s%d", fusedata, idxblockn);
		fp = fopen(filename, "r");
		char blocklist_info[1700];
		memset(blocklist_info, '\0', sizeof(blocklist_info));
		r = fread(blocklist_info, 1, 1700, fp);
		fclose(fp);

		lastfileblockn = blocklist(blocklist_info, 1);
		blocktaken = blocklist(blocklist_info, 2);
	}

	sprintf(filename, "%s%d", fusedata, lastfileblockn);
	FILE *fp;
	fp = fopen(filename, "r");
	char lastfile_cont[4097];
	memset(lastfile_cont, '\0', sizeof(lastfile_cont));

	r = fread(lastfile_cont, 1, 4096, fp);
	fclose(fp);
	
	char filecontent[100000];
	char buff[100000];
	strcpy(buff, buf);
	buff[size] = '\0';

	strcpy(filecontent, lastfile_cont);
	strcat(filecontent, buff);
	
	int content_len = strlen(filecontent);
	
	int newblock_num = (content_len + (BLOCK_SIZE - 1)) / BLOCK_SIZE - 1;

	if (newblock_num + blocktaken > MAX_FILE_BLOCK) {
		return -EFBIG;
	}

	if (content_len < 4096) {
		write_file(lastfileblockn, filecontent, 0, content_len - 1);

	}
	else {
		write_file(lastfileblockn, filecontent, 0, 4095);
	}

	if (newblock_num > 0) {
		for (i = 1; i <= newblock_num; i++) {
			firstblock = find_first_freeblock();
			if (firstblock == -1) {
				return -ENOSPC;
			}
			newblock[i - 1] = firstblock;
			empty_file(firstblock);
			if (4096 * (i + 1) - 1 > content_len) {
				write_file(firstblock, filecontent, 4096 * i, content_len - 1);
			}
			else {
			write_file(firstblock, filecontent, 4096 * i, 4096 * (i + 1) - 1);
			}
		}
		if (block[inoden].indirect == 0) {
			firstblock = find_first_freeblock();
			if (firstblock == -1) {
				return -ENOSPC;
			}
			block[inoden].indirect = 1;
			int originalocation = block[inoden].location;
			block[inoden].location = firstblock;			
			empty_file(block[inoden].location);
			sprintf(filename, "%s%d", fusedata, block[inoden].location);
			FILE *fp2;
			fp2 = fopen(filename, "a+");
			fprintf(fp2, "%d,", originalocation);
			for (i = 0; i < newblock_num; i++) {
				fprintf(fp2, " %d,", newblock[i]);
			}
			fclose(fp2);
		}
		else {
			sprintf(filename, "%s%d", fusedata, block[inoden].location);
			FILE *fp2;
			fp2 = fopen(filename, "a+");
			for (i = 0; i < newblock_num; i++) {
				fprintf(fp2, " %d,", newblock[i]);
			}
			fclose(fp2);
		}
	}

	block[inoden].size += size;
	int parent_inode = find_parent_inode(path);
	char *name = split_to_name(path);
	int idxinparentino = find_name_in_inode(block[parent_inode], name);
	int blockn = block[parent_inode].filename_to_inode_dict[idxinparentino].inode;
	write_file_inode(block[inoden], blockn);

	(void) fi;
	(void) r;
	(void) offset;
	return size;	
}            

static void* vfs_init(struct fuse_conn_info *conn)
{	
	int i, r;
	char filename[50];
	memset(zero, '0', (size_t) BLOCK_SIZE);
	
	for (i = 0; i < MAX_BLOCK_NUM; i++) {
	    FILE *fp;
	    sprintf(filename, "%s%d", fusedata, i);
	    fp = fopen(filename, "w");
            fprintf(fp, "%s", zero);
	    fclose(fp);
	}
	initial_freeblock();

	// init Superblock
	Superblock.creationTime = (int) time(NULL);
	Superblock.mounted = 50;
	Superblock.devId = 20;
	Superblock.freeStart = 1;
	Superblock.freeEnd = 25;
	Superblock.root = 26;
	Superblock.maxBlocks = MAX_BLOCK_NUM;
	Superblock.freeblocks = MAX_BLOCK_NUM - 27;
	Superblock.freeinodes = MAX_INODE_NUM;

	FILE *fp;
	sprintf(filename, "%s0", fusedata);
	fp = fopen(filename, "r+");
	
	fprintf(fp, "{creationTime:%d, mounted:%d, devId:%d, freeStart:%d, freeEnd:%d, root:%d, maxBlocks:%d", 
		    Superblock.creationTime, Superblock.mounted, Superblock.devId, Superblock.freeStart, 
		    Superblock.freeEnd, Superblock.root, Superblock.maxBlocks);
	
	fputs("}", fp);
	fclose(fp);
	
	// init root inode
	block[26].size = 4096;
	block[26].uid = 1;
	block[26].gid = 1;
	block[26].mode = 16877;
	block[26].atime = (int) time(NULL);
	block[26].ctime = (int) time(NULL);
	block[26].mtime = (int) time(NULL);
	block[26].linkcount = 2;
	block[26].subn = 2;
	strcpy(block[26].filename_to_inode_dict[0].name, ".");
	block[26].filename_to_inode_dict[0].type = 'd';
	block[26].filename_to_inode_dict[0].inode = 26;
	strcpy(block[26].filename_to_inode_dict[1].name, "..");
	block[26].filename_to_inode_dict[1].type = 'd';
	block[26].filename_to_inode_dict[1].inode = 26;
	write_dir_inode(block[26]);

	
	(void) conn;
	(void) r;
	return 0;
}

static int vfs_rename(const char* from, const char* to)
{
	int i, j, isFile = 1;
	int from_parent_inode = find_parent_inode(from);
	int to_parent_inode = find_parent_inode(to);
	int from_inode = split_to_blockn(from, 0);

	char from_name[MAX_NAME_LEN];
	char to_name[MAX_NAME_LEN];
	
	strcpy(from_name, split_to_name(from));
	strcpy(to_name, split_to_name(to));
	
	int to_name_idx = find_name_in_inode(block[to_parent_inode], to_name);
	int from_name_idx = find_name_in_inode(block[from_parent_inode], from_name);
	
	if (to_name_idx != 0) {
		return -EEXIST;
	}
	if (from_name_idx == 0) {
		return -ENOENT;
	}
	if (block[from_parent_inode].filename_to_inode_dict[from_name_idx].type == 'd') {
		isFile = 0;
	}

	if (to_name_idx == 0) {
		j = block[to_parent_inode].subn;
		block[to_parent_inode].filename_to_inode_dict[j] = block[from_parent_inode].filename_to_inode_dict[from_name_idx];
		strcpy(block[to_parent_inode].filename_to_inode_dict[j].name, to_name); 
		block[to_parent_inode].subn++;
		if (isFile == 0) {
			block[to_parent_inode].linkcount++;
		}
		write_dir_inode(block[to_parent_inode]);
	}
	
	// modify from inode if it is a directory
	if (isFile == 0) {
		block[from_inode].filename_to_inode_dict[1].inode = block[to_parent_inode].filename_to_inode_dict[0].inode;
		write_dir_inode(block[from_inode]); 
	}
	
	// modify from_parent inode	
	if (from_name_idx != 0) {
		if (isFile == 0) {
			block[from_parent_inode].linkcount--;
		}

		if (from_name_idx == block[from_parent_inode].subn - 1) {
			block[from_parent_inode].filename_to_inode_dict[from_name_idx] 
				= block[from_parent_inode].filename_to_inode_dict[block[from_parent_inode].subn];
		}
		else {
			for(i = from_name_idx; i < MAX_FILE_NUM; i++) {
				block[from_parent_inode].filename_to_inode_dict[i] = block[from_parent_inode].filename_to_inode_dict[i + 1];
			}
		}
	
		block[from_parent_inode].subn--;
		write_dir_inode(block[from_parent_inode]);
	}	

	return 0;
}

static int vfs_link(const char* from, const char* to)
{
	int to_parent_inode = find_parent_inode(to);
	int from_inode = split_to_blockn(from, 0);
	char from_name[MAX_NAME_LEN], to_name[MAX_NAME_LEN];
	strcpy(from_name, split_to_name(from));
	strcpy(to_name, split_to_name(to));

	int j = block[to_parent_inode].subn;
	block[to_parent_inode].filename_to_inode_dict[j].type = 'f';
	strcpy(block[to_parent_inode].filename_to_inode_dict[j].name, to_name);
	block[to_parent_inode].filename_to_inode_dict[j].inode = from_inode;
	block[to_parent_inode].subn++;
	block[from_inode].linkcount++;

	write_dir_inode(block[to_parent_inode]);
	write_file_inode(block[from_inode], from_inode);

	return 0;	
}

static int vfs_unlink(const char* path)
{
	int i;
	int parent_inoden = find_parent_inode(path);
	int inoden = split_to_blockn(path, 0);
	char* name = split_to_name(path);
	int idxinparentino = find_name_in_inode(block[parent_inoden], name);

	block[inoden].linkcount--;
	if (block[inoden].linkcount == 0) {
		remove_file(inoden);	
	}

	if (idxinparentino == block[parent_inoden].subn - 1) {
			block[parent_inoden].filename_to_inode_dict[idxinparentino] 
				= block[parent_inoden].filename_to_inode_dict[idxinparentino + 1];
		}
		else {
			for(i = idxinparentino; i < MAX_FILE_NUM; i++) {
				block[parent_inoden].filename_to_inode_dict[i] = block[parent_inoden].filename_to_inode_dict[i + 1];
			}
	}

	block[parent_inoden].subn--;
	write_dir_inode(block[parent_inoden]);
	return 0;
}

static int vfs_rmdir(const char* path)
{
	int i;
	int inode = split_to_blockn(path, 0);
	int parent_inoden = find_parent_inode(path);
	char* name = split_to_name(path);
	int idxinparentino = find_name_in_inode(block[parent_inoden], name);

	if (idxinparentino == block[parent_inoden].subn - 1) {
			block[parent_inoden].filename_to_inode_dict[idxinparentino] 
				= block[parent_inoden].filename_to_inode_dict[idxinparentino + 1];
		}
		else {
			for(i = idxinparentino; i < MAX_FILE_NUM; i++) {
				block[parent_inoden].filename_to_inode_dict[i] = block[parent_inoden].filename_to_inode_dict[i + 1];
			}
	}
	memset(&block[inode], 0, sizeof(block[inode]));
	restore_freeblock(inode);
	block[parent_inoden].subn--;
	block[parent_inoden].linkcount--;
	write_dir_inode(block[parent_inoden]);
	return 0;
}

static int vfs_statfs(const char* path, struct statvfs* stbuf)
{
	stbuf->f_bsize = BLOCK_SIZE;
	stbuf->f_frsize = BLOCK_SIZE;
	stbuf->f_blocks = MAX_BLOCK_NUM;
	stbuf->f_bfree = Superblock.freeblocks;
	stbuf->f_bavail = Superblock.freeblocks;
	stbuf->f_files = MAX_INODE_NUM;
	stbuf->f_ffree = Superblock.freeinodes;
	stbuf->f_favail = Superblock.freeinodes;
	stbuf->f_fsid = 2970;
	stbuf->f_flag = 0;
	stbuf->f_namemax = MAX_NAME_LEN;

	(void) path;
	return 0;
}

static void vfs_destroy(void * fs_data)
{
	(void) fs_data;
	int i;
	char filename[30];
	for (i = 0; i < MAX_BLOCK_NUM; i++) {	    
	    sprintf(filename, "%s%d", fusedata, i);
	    unlink(filename);
	}
	memset(&Superblock, 0, sizeof(Superblock));
	memset(&block, 0, MAX_BLOCK_NUM * sizeof(struct inode));
}

// implement following functions to make successful getattr 
static int vfs_chmod(const char* path, mode_t mode)
{
	(void) path;
	(void) mode;
	return 0;
}

static int vfs_chown(const char* path, uid_t uid, gid_t gid)
{
	(void) path;
	(void) uid;
	(void) gid;
	return 0;
}

static int vfs_utimens(const char* path, const struct timespec ts[2])
{
	(void) path;
	(void) ts;
	return 0;
}

static int vfs_truncate(const char* path, off_t size)
{
	int r;
	char filename[30];
	char blocklist_info[1700];
	memset(blocklist_info, '\0', sizeof(blocklist_info));
	
	int inoden = split_to_blockn(path, 0);
	
	if (block[inoden].indirect == 0) {
		empty_file(block[inoden].location);		
	}
	else {
		block[inoden].indirect = 0;
		sprintf(filename, "%s%d", fusedata, block[inoden].location);
		FILE *fp;
		fp = fopen(filename, "r");
		r = fread(blocklist_info, 1, 1700, fp);
		fclose(fp);
		r = blocklist(blocklist_info, 3);

		empty_file(block[inoden].location);
	}
	
	int parent_inode = find_parent_inode(path);
	char *name = split_to_name(path);
	int idxinparentino = find_name_in_inode(block[parent_inode], name);
	int blockn = block[parent_inode].filename_to_inode_dict[idxinparentino].inode;
	block[inoden].size = 0;
	write_file_inode(block[inoden], blockn);

	(void) size;
	(void) r;
	return 0;
}


static struct fuse_operations vfs_oper = {
	.getattr	= vfs_getattr,
	.opendir    = vfs_opendir,
	.readdir	= vfs_readdir,
	.releasedir = vfs_releasedir,
	.open		= vfs_open,
	.read		= vfs_read,
	.init		= vfs_init,
	.create	    = vfs_create,
	.mkdir      = vfs_mkdir,
	.rmdir      = vfs_rmdir,
	.rename     = vfs_rename,
	.release    = vfs_release,
	.write      = vfs_write,
	.link       = vfs_link,
	.unlink     = vfs_unlink,
	.statfs     = vfs_statfs,
	.chmod      = vfs_chmod,
	.chown      = vfs_chown,
	.utimens    = vfs_utimens,
	.truncate   = vfs_truncate,
	.destroy    = vfs_destroy,
};

int main(int argc, char *argv[])
{	
	return fuse_main(argc, argv, &vfs_oper, NULL);
}
