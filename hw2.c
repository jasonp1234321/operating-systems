#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>

typedef struct {
    int row;
    int col;
} Move;

int getVal(int **board, int r, int c) {
    return *(*(board + r) + c);
}

void setVal(int **board, int r, int c, int val) {
    *(*(board + r) + c) = val;
}

int isValid(int **board, int m, int n, int r, int c) {
    return r >= 0 && r < m && c >= 0 && c < n && getVal(board, r, c) == 0;
}

int **createBoard(int m, int n) {
    int **board = (int **)calloc(m, sizeof(int *));
    if (!board) {
        perror("ERROR: calloc failed");
        exit(EXIT_FAILURE);
    }

    int i = 0;
    while (i < m) {
        *(board + i) = (int *)calloc(n, sizeof(int));
        if (!(*(board + i))) {
            perror("ERROR: calloc failed");
            exit(EXIT_FAILURE);
        }
        i++;
    }

    return board;
}

void copyBoard(int **src, int **dst, int m, int n) {
    int i = 0;
    while (i < m) {
        int j = 0;
        while (j < n) {
            setVal(dst, i, j, getVal(src, i, j));
            j++;
        }
        i++;
    }
}

void freeBoard(int **board, int m) {
    int i = 0;
    while (i < m) {
        free(*(board + i));
        i++;
    }
    free(board);
}

void solve(int **board, int m, int n, int r, int c, int moveNum, int *pipeFd, pid_t rootPid) {
    setVal(board, r, c, moveNum);

    int *knightMoves = (int *)calloc(16, sizeof(int));
    *(knightMoves + 0) = -1; *(knightMoves + 1) = 2;
    *(knightMoves + 2) = 1;  *(knightMoves + 3) = 2;
    *(knightMoves + 4) = 2;  *(knightMoves + 5) = 1;
    *(knightMoves + 6) = 2;  *(knightMoves + 7) = -1;
    *(knightMoves + 8) = 1;  *(knightMoves + 9) = -2;
    *(knightMoves +10) = -1; *(knightMoves +11) = -2;
    *(knightMoves +12) = -2; *(knightMoves +13) = -1;
    *(knightMoves +14) = -2; *(knightMoves +15) = 1;

    Move *validMoves = (Move *)calloc(8, sizeof(Move));
    int count = 0, i = 0;

    while (i < 8) {
        int nr = r + *(knightMoves + i * 2);
        int nc = c + *(knightMoves + i * 2 + 1);
        if (isValid(board, m, n, nr, nc)) {
            (*(validMoves + count)).row = nr;
            (*(validMoves + count)).col = nc;
            count++;
        }
        i++;
    }

    #ifndef QUIET
    if (count > 1) {
        printf("P%d: %d possible moves after move #%d; creating %d child processes...\n", getpid(), count, moveNum, count);
    }
    #endif

    if (count == 0) {
        int total = m * n;
        if (moveNum == total) {
            int isClosed = 0, j = 0;
            while (j < 8) {
                int nr = r + *(knightMoves + j * 2);
                int nc = c + *(knightMoves + j * 2 + 1);
                if (nr >= 0 && nr < m && nc >= 0 && nc < n && getVal(board, nr, nc) == 1) {
                    isClosed = 1;
                    break;
                }
                j++;
            }
            dprintf(*(pipeFd + 1), "%d\n", isClosed ? 2 : 1);
            printf("P%d: Found an %s knight's tour; notifying top-level parent process\n", getpid(), isClosed ? "CLOSED" : "OPEN");
        } else {
            #ifndef QUIET
            printf("P%d: Dead end at move #%d\n", getpid(), moveNum);
            #endif
        }

        free(validMoves);
        free(knightMoves);
        freeBoard(board, m);
        exit(moveNum);
    }

    int maxCovered = moveNum;
    pid_t *childPids = (pid_t *)calloc(count, sizeof(pid_t));
    int *statuses = (int *)calloc(count, sizeof(int));

    i = 0;
    while (i < count) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("ERROR: fork failed");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            int **newBoard = createBoard(m, n);
            copyBoard(board, newBoard, m, n);
            solve(newBoard, m, n, (*(validMoves + i)).row, (*(validMoves + i)).col, moveNum + 1, pipeFd, rootPid);
        } else {
            *(childPids + i) = pid;

            #ifdef NO_PARALLEL
            int status;
            waitpid(pid, &status, 0);
            *(statuses + i) = WEXITSTATUS(status);
            if (*(statuses + i) > maxCovered) maxCovered = *(statuses + i);
            #endif
        }
        i++;
    }

    #ifndef NO_PARALLEL
    i = 0;
    while (i < count) {
        int status;
        waitpid(*(childPids + i), &status, 0);
        *(statuses + i) = WEXITSTATUS(status);
        if (*(statuses + i) > maxCovered) maxCovered = *(statuses + i);
        i++;
    }
    #endif

    free(validMoves);
    free(knightMoves);
    free(childPids);
    free(statuses);
    freeBoard(board, m);
    exit(maxCovered);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc != 5) {
        fprintf(stderr, "ERROR: Incorrect argument(s)\n");
        fprintf(stderr, "USAGE: ./hw2.out <m> <n> <r> <c>\n");
        return EXIT_FAILURE;
    }

    int m = atoi(*(argv + 1));
    int n = atoi(*(argv + 2));
    int r = atoi(*(argv + 3));
    int c = atoi(*(argv + 4));

    if (m <= 1 || n <= 1 || r < 0 || r >= m || c < 0 || c >= n) {
        fprintf(stderr, "ERROR: Incorrect argument(s)\n");
        fprintf(stderr, "USAGE: ./hw2.out <m> <n> <r> <c>\n");
        return EXIT_FAILURE;
    }

    printf("P%d: Solving the knight's tour problem for %dx%d board", getpid(), m, n);
    #ifdef NO_PARALLEL
    printf(" %cNO_PARALLEL%c", 91, 93);
    #endif
    printf("\n");

    printf("P%d: Starting at row %d and column %d (move #1)\n", getpid(), r, c);

    int *pipeFd = (int *)calloc(2, sizeof(int));
    if (pipe(pipeFd) == -1) {
        perror("ERROR: pipe failed");
        return EXIT_FAILURE;
    }

    int **board = createBoard(m, n);
    pid_t rootPid = getpid();

    pid_t pid = fork();
    if (pid < 0) {
        perror("ERROR: fork failed");
        return EXIT_FAILURE;
    }

    int status;
    int maxCovered = 0;

    if (pid == 0) {
        close(*(pipeFd + 0));
        solve(board, m, n, r, c, 1, pipeFd, rootPid);
    } else {
        close(*(pipeFd + 1));
        freeBoard(board, m);
        waitpid(pid, &status, 0);
        maxCovered = WEXITSTATUS(status);
    }

    int open = 0, closed = 0;
    FILE *stream = fdopen(*(pipeFd + 0), "r");
    char *buf = (char *)calloc(16, sizeof(char));
    while (fgets(buf, 16, stream)) {
        int val = atoi(buf);
        if (val == 1) open++;
        else if (val == 2) closed++;
    }
    fclose(stream);
    free(buf);
    free(pipeFd);

    if (open == 0 && closed == 0) {
        printf("P%d: Search complete; partial solution(s) visited %d out of %d squares\n", getpid(), maxCovered, m * n);
    } else {
        printf("P%d: Search complete; found %d open tours and %d closed tours\n", getpid(), open, closed);
    }

    return 0;
}
