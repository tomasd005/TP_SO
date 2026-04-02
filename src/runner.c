#include <stdio.h>
#include <string.h>

void print_usage(void)
{
    printf("usage:\n");
    printf("./runner -e <user-id> <command> [args...]\n");
    printf("./runner .c\n");
    printf("./runner -s\n");
}
int main(int argc, char *argv[])
{
    if (argc < 2) // caso de uma entrada do tipo "./runner", logo nao e valido
    {
        print_usage();
        return 1;
    }
    if (strcmp(argv[1], "-e") == 0)
    {
        if (argc < 4) // para usar este comando tem que ser dados dois dados adicionais o user-id e o command
        {
            print_usage();
            return 1;
        }
        printf("modo execucao\n");
        printf("user-id; %s\n", argv[2]);
        printf("comando: %s\n", argv[3]);
        return 0;
    }

    if (strcmp(argv[1], "-c") == 0)
    {
        if (argc != 2) 
        {
            print_usage();
            return 1;
        }
        printf("modo consulta\n");
        return 0;
    }
    if (strcmp(argv[1], "-s") == 0)
    {
        if (argc != 2)
        {
            print_usage();
            return 1;
        }

        printf("modo shutdown\n");
        return 0;
    }

    print_usage();
    return 1;

    return 0;
}
