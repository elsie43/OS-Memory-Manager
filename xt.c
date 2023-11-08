#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
int main()
{

    FILE *fp;
    char ch;
    char numstr[10];

    int **PT = malloc(20 * sizeof *PT);
    for (int i = 0; i < 20; i++)
    {
        PT[i] = malloc(20 * sizeof *PT[i]);
    }
    for (int i = 0; i < 20; i++)
    {
        PT[i][i] = i;
    }
    printf("%d\n", PT[5][5]);
    return 0;
}