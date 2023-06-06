#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const int BLOCK_SIZE = 512;
const int NUM_BLOCK = 64;

const int BLOCK_NUMBER_INODE_BITMAP_BLOCK = 1;
const int BLOCK_NUMBER_DATA_BITMAP_BLOCK_ = 1;
const int BLOCK_NUMBER_INODE_BLOCK = 2;
const int BLOCK_NUMBER_DATA_BLOCK = 4;

const int BASE_INODE_BITMAP = 0;
const int BASE_DATA_BITMAP = 256;

const int EMPTY_INUM = 0;
const int BAD_INUM = 1;
const int ROOT_DIR_INUM = 2;

typedef struct inode {
    unsigned int fsize; // how many bytes in file
    unsigned int blocks; // how many allocated blocks
    unsigned int dptr; // direct pointer
    unsigned int iptr; // indirect pointer
} inode;

typedef struct dir_record {
    char inum;
    char name[3];
} dir_record;

const int NUM_MAX_FILES = 61;

typedef struct bitmap {
    char *map;
    int last;
    int size;
} bitmap;

typedef char data_block[512];

char disk[NUM_BLOCK][BLOCK_SIZE];
bitmap inode_bitmap;
bitmap data_bitmap;
inode *inode_blocks;
data_block *data_blocks;


void disk_init();

int add_on_bitmap(bitmap *bm);

void delete_on_bitmap(bitmap *bm, char inum);

int free_count(bitmap *bm);

void read(char *filename, int size);

void write(char *filename, int size);

int more_block_count(char inum, int size);

void create(char *filename, int size);

void delete(char *filename);

int main(int argc, char **argv) {

    if (argc != 2) exit(1);

    FILE *input = fopen(argv[1], "r");

    char buf[1024];
    while (!feof(input)) {
        fgets(buf, 1024, input);
        char *filename, *command, *size;
        filename = strtok(input, " ");
        command = strtok(NULL, " ");

        if (strcmp(command, "w")) {
            size = strtok(NULL, " ");
            write(filename, atoi(size));
        } else if(strcmp(command,"r")) {
            size = strtok(NULL, " ");
            write(filename, atoi(size));
        } else {
            delete(filename);
        }
    }

    disk_init();
    return 0;
}

void read(char *filename, int size) {
    dir_record *records = (dir_record *) (&data_blocks[inode_blocks[ROOT_DIR_INUM].dptr]);
    char inum = 0;
    for (int i = 0; i < NUM_MAX_FILES; i++) {
        if (!strcmp(records[i].name, filename)) {
            inum = records[i].inum;
            break;
        }
    }

    if (!inum) {
        printf("No such file\n");
        return;
    }

    inode curr = inode_blocks[inum];
    unsigned int dptr = curr.dptr;
    unsigned int iptr = curr.iptr;
    unsigned int fsize = curr.fsize;
    unsigned int blocks = curr.blocks;

    printf("dptr=%d, iptr=%d, fsize=%d, blocks=%d\n", dptr, iptr, fsize, blocks);

    int read_size = size > fsize ? fsize : size;

    if (iptr) {
        unsigned int *list = (unsigned int *) (&data_blocks[iptr]);
        int size_count = read_size;
        for (int i = 0; i < blocks - 1 && size_count; i++) {
            for (int j = 0; j < BLOCK_SIZE && size_count; j++, size_count--)
                printf("%c", data_blocks[list[i]][j]);
        }
    } else {
        for (int i = 0; i < read_size; i++) {
            printf("%c", data_blocks[dptr][i]);
        }
    }

    printf("\n");
}

void write(char *filename, int size) {
    dir_record *records = (dir_record *) (&data_blocks[inode_blocks[ROOT_DIR_INUM].dptr]);
    char inum = 0;
    for (int i = 0; i < NUM_MAX_FILES; i++) {
        if (!strcmp(records[i].name, filename)) {
            inum = records[i].inum;
            break;
        }
    }

    if (!inum) {
        create(filename, size);
    } else {
        inode *curr_inode = &inode_blocks[inum];

        int mc = more_block_count(inum, size);

        if (mc == 0) {
            data_block *block = &data_blocks[curr_inode->dptr];

            int base = 0;
            while ((*block)[base] != 0) base++;

            for (int i = 0; i < size; i++)
                (*block)[base + i] = filename[0];
        } else {
            if (mc > free_count(&data_bitmap)) {
                printf("No space\n");
                return;
            }

            curr_inode->blocks += mc;
            curr_inode->fsize += size;

            int size_count = size;
            int base = 0;
            int idx = 0;

            if (curr_inode->dptr) {
                data_block *block = &data_blocks[curr_inode->dptr];

                while ((*block)[base] != 0) base++;

                while (size_count && base + idx < BLOCK_SIZE) {
                    (*block)[base + idx] = filename[0];
                    idx++;
                    size_count--;
                }

                if (!size_count && base + idx != BLOCK_SIZE)
                    (*block)[base + idx] = 0;
            }

            if (!size_count) return;
            if (base + idx == BLOCK_SIZE) {
                curr_inode->iptr = add_on_bitmap(&data_bitmap);
                unsigned int *dptr_list = (unsigned int *) (&data_blocks[curr_inode->iptr]);
                dptr_list[0] = curr_inode->dptr;
                curr_inode->dptr = 0;
                mc--;
            }

            unsigned int *dptr_list = (unsigned int *) (&data_blocks[curr_inode->iptr]);
            base = 0;

            while (dptr_list[base]) base++;

            for (int i = 0; i < mc && size_count; i++) {
                dptr_list[base + i] = add_on_bitmap(&data_bitmap);
                data_block (*block) = &data_blocks[dptr_list[base + i]];
                for (int j = 0; j < BLOCK_SIZE && size_count; j++, size_count--)
                    (*block)[j] = filename[0];
                if (i == mc - 1 && size % BLOCK_SIZE != 0)
                    (*block)[size % BLOCK_SIZE] = 0;
            }
        }
    }
}

int more_block_count(char inum, int size) {
    inode *curr_inode = &inode_blocks[inum];
    unsigned int dptr = curr_inode->dptr;
    unsigned int iptr = curr_inode->iptr;
    unsigned int blocks = curr_inode->blocks;
    if (iptr) {
        unsigned int *dptr_list = (unsigned int *) (&data_blocks[iptr]);

        int idx_last_block = -1;
        while (dptr_list[idx_last_block + 1] != 0 && idx_last_block < blocks) idx_last_block++;

        int cnt = 0;
        while (data_blocks[idx_last_block][cnt] != 0) cnt++;

        if (BLOCK_SIZE >= size + cnt) return 0;
        else return ((size - (BLOCK_SIZE - cnt)) / BLOCK_SIZE) + 1;
    } else {
        int cnt = 0;
        while (data_blocks[dptr][cnt] != 0) cnt++;

        if (BLOCK_SIZE > size + cnt) return 0;
        else if (BLOCK_SIZE == size + cnt) return 1;
        else return ((size - (BLOCK_SIZE - cnt)) / BLOCK_SIZE) + 2;
    }
}

void create(char *filename, int size) {
    int inum = add_on_bitmap(&inode_bitmap);

    if (inum == -1) {
        printf("No space\n");
        return;
    }

    int required_size = (size / BLOCK_SIZE) + 1;
    int fc = free_count(&data_bitmap);

    if (fc < required_size) {
        delete_on_bitmap(&inode_bitmap, inum);
        printf("No space\n");
        return;
    }

    dir_record *records = (dir_record *) (&data_blocks[inode_blocks[ROOT_DIR_INUM].dptr]);
    for (int i = 0; i < records; i++) {
        if (records[i].inum == 0) {
            records[i].inum = inum;
            records[i].name[0] = filename[0];
            records[i].name[1] = filename[1];
            break;
        }
    }

    inode_blocks[inum].fsize = size;
    inode_blocks[inum].blocks = required_size;

    if (required_size == 1) {
        inode_blocks[inum].dptr = add_on_bitmap(&data_bitmap);
        data_block *block = &data_blocks[inode_blocks[inum].dptr];

        for (int i = 0; i < size; i++)
            (*block)[i] = filename[0];
        if (size != BLOCK_SIZE) (*block)[size] = 0;
    } else {
        inode_blocks[inum].iptr = add_on_bitmap(&data_bitmap);
        unsigned int *dptr_list = (unsigned int *) (&data_blocks[inode_blocks[inum].iptr]);
        int size_count = size;
        for (int i = 0; i < required_size; i++) {
            dptr_list[i] = add_on_bitmap(&data_bitmap);
            data_block *curr_block = &data_blocks[dptr_list[i]];
            for (int j = 0; j < BLOCK_SIZE && size_count; j++, size_count--)
                (*curr_block)[j] = filename[0];
            if (i == required_size - 1 && size % BLOCK_SIZE != 0)
                (*curr_block)[size % BLOCK_SIZE] = 0;
        }
    }
}

void delete(char *filename) {
    dir_record *records = (dir_record *) (&data_blocks[inode_blocks[ROOT_DIR_INUM].dptr]);
    char inum = 0;
    for (int i = 0; i < NUM_MAX_FILES; i++) {
        if (!strcmp(records[i].name, filename)) {
            inum = records[i].inum;
            records[i].inum = 0;
            break;
        }
    }

    if (!inum) {
        printf("No such file\n");
        return;
    }

    inode curr = inode_blocks[inum];
    unsigned int dptr = curr.dptr;
    unsigned int iptr = curr.iptr;

    if (dptr) {
        delete_on_bitmap(&data_bitmap, dptr);
    }

    if (iptr) {
        unsigned int *dptr_list = (unsigned int *) (&data_blocks[iptr]);
        for (int i = 0; i < curr.blocks - 1; i++) {
            delete_on_bitmap(&data_bitmap, dptr_list[i]);
        }
    }

    // 남아있는 메모리에 대해서는 어떻게 관리할건가
    // 일단 create 할 때 0 해서 충돌나지 않도록 최적화
}

void disk_init() {
    inode_bitmap.last = 0;
    data_bitmap.last = 0;

    inode_bitmap.size = 64;
    data_bitmap.size = 60;

    inode_bitmap.map = &disk[BLOCK_NUMBER_INODE_BITMAP_BLOCK][BASE_INODE_BITMAP];
    data_bitmap.map = &disk[BLOCK_NUMBER_DATA_BITMAP_BLOCK_][BASE_DATA_BITMAP];
    inode_blocks = (inode *) (&disk[BLOCK_NUMBER_INODE_BLOCK]);
    data_blocks = (data_block *) (&disk[BLOCK_NUMBER_DATA_BLOCK]);

    add_on_bitmap(&inode_bitmap); // reserve inum0
    add_on_bitmap(&inode_bitmap); // reserve inum1

    // reserve root
    add_on_bitmap(&inode_bitmap);

    inode *root = &inode_blocks[ROOT_DIR_INUM];
    root->fsize = sizeof(dir_record) * NUM_MAX_FILES;
    root->blocks = 1;
    root->dptr = add_on_bitmap(&data_bitmap);

    dir_record *records = (dir_record *) (&data_blocks[root->dptr]);
    for (int i = 0; i < NUM_MAX_FILES; i++) {
        records[i].inum = EMPTY_INUM;
    }
}

int add_on_bitmap(bitmap *bm) {

    if (bm->last == bm->size) return -1;

    int last = bm->last;
    bm->map[last] = 1;

    for (int i = 0; i < bm->size; i++) {
        if (!bm->map[i]) {
            bm->last = i;
            break;
        }
        if (i == bm->size - 1) {
            bm->last = bm->size; // full
        }
    }

    return last;
}

void delete_on_bitmap(bitmap *bm, char idx) {
    bm->map[idx] = 0;
    bm->last = idx;
}

int free_count(bitmap *bm) {
    int res = 0;
    for (int i = 0; i < bm->size; i++) {
        if (!bm->map[i]) res++;
    }
    return res;
}
