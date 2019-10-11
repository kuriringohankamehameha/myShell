#include "shell.h"
#include <ncurses.h>

#define gotoxy(x, y) printf("\033[%d;%dH", (y), (x))
#define ALT_BACKSPACE 127
#define MAX_WORDS 10000

typedef struct Node_t Node;

struct Node_t{
    char data;
    Node* child[27];
    int isEnd;
};

char alphabets[] = {'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',' '};

static int completion_count = 0;
static char** completion_arr;
int tab_count = 0;

/* Reference for our ncurses Window  and x,y coordinates of it's cursor */
WINDOW* win;
int x, y;

void backspace()
{
    /* Set No Echo mode */
    noecho();
    nocbreak();
    
    /* Goto current cursor position and delete the previous character */
    getyx(win, y, x);
    move(y, x-1);
    delch();

    cbreak();
    refresh();
}

Node* newNode()
{
    Node* head = (Node*) malloc (sizeof(Node));
    head->isEnd = 0;
    
    for(int i=0; i<27; i++)
        head->child[i] = NULL;
    return head;
}

void freeNode(Node* node)
{
    for(int i=0; i<27; i++)
    {
        if (node->child[i] != NULL)
            freeNode(node->child[i]);
        else
        {
            free(node);
            break;
        }
    }       
}

Node* insertTrie(Node* head, char* str)
{
    /* Inserts the String into the Trie */
    Node* temp = head;
    for (int i=0; str[i] != '\0'; i++)
    {
        int position = str[i] != ' ' ? str[i] - 'a' : 26;
        
        if (temp->child[position] == NULL)
            temp->child[position] = newNode();

        temp = temp->child[position];
    }
    temp->isEnd = 1;
    return head;
}

int searchTrie(Node* head, char* str)
{
    Node* temp = head;

    for(int i=0; str[i]!='\0'; i++)
    {
        int position = str[i] - 'a';
        if (temp->child[position] == NULL)
            return 0;
        temp = temp->child[position];
    }

    if (temp != NULL && temp->isEnd == 1)
        return 1;
    return 0;
}

char* str_concat(char* s1, char s2)
{
    char* op = (char*) malloc (sizeof(char));
    int k = 0;
    for(int i=0; i<strlen(s1)+1; i++)
    {
        if (i < strlen(s1))
            op[k++] = s1[i];
        else
            op[k++] = s2;
    }
    op[k] = '\0';
    return op;
}

void recursiveInsert(Node* temp, int start, char* op)
{
    if (start >= 27)
        return;
    if(temp){
        if (temp->isEnd)
        {
            strcpy(completion_arr[completion_count], op);
            completion_count ++;
            printw("%s\n", op);
        }
        for(int i=start; i<27; i++)
        {
            if (temp->child[i])
            {
                Node* match_child;
                for (int j=0; j<27; j++)
                {
                    match_child = temp->child[j];
                    recursiveInsert(match_child, 0, str_concat(op, alphabets[j]));
                }
                break;
            }
        }
    }
}

int completeWord(Node* head, char* str, char* op)
{
    Node* temp = head;
    for(int i=0; str[i]!='\0'; i++)
    {
        int position = str[i] - 'a';
        if (temp->child[position] == NULL)
            return 0;
        temp = temp->child[position];
    }

    if (temp != NULL)
    {
        printw("\n");
        recursiveInsert(temp, 0, op);
        return 1;
    }   
    return 0;
}

void clear_complete()
{
    /* Clears the previous autocomplete words from the Screen */
    if (completion_count == 0)
        return;
    for(int i=0; i<completion_count; i++)
    {
        move(y+1+i, 0);
        clrtoeol();
    }
    completion_count = 0;
    move(y, x);
}

char* select_word()
{
    /* Selects a word from the autocomplete list */
    if (completion_count < tab_count)
        return NULL;
    return completion_arr[tab_count];
}

int find_space()
{
    /* Replaces the current cursor with tabbed_op */
    char delim = ' ';
    int x_curr = x-1;
    int y_curr = y;
    while(x_curr >= 0)
    {
        if (mvinch(y_curr, x_curr) == delim)
        {
            return x_curr;
        }
        else
            x_curr --;
    }
    return 0;
}

void replace_word(int pos, char* tabbed_op)
{
    /* Replaces word at pos(x0, y0) with tabbed_op */
    int y0 = y;
    int x0 = pos != 0 ? pos + 1 : pos;
   
    move(y0, x0);
    x = x0;
    clrtoeol();
    
    for(int i=0; tabbed_op[i]!='\0'; i++)
        printw("%c", tabbed_op[i]);
}

int main()
{
    win = initscr();
    cbreak();
    noecho();

    /* Engine Code here */
    char* samples[] = {"thesis","omg", "theresa", "thevenin", "thames river", "lol"};
    int n = sizeof(samples)/sizeof(samples[0]);
    
    Node* root = newNode();
    for(int i=0; i<n; i++)
        insertTrie(root, samples[i]);

    char* op = (char*) malloc (sizeof(char));

    int ch;
    int k;
    int tab_check = 0;
    int xpos = -1;
    int ypos = -1;

    completion_arr = (char**) malloc (MAX_WORDS * sizeof(char*));
    for(int i=0; i<MAX_WORDS; i++)
        completion_arr[i] = (char*) malloc (sizeof(char));

    char* tabbed_op = (char*) malloc (sizeof(char));
    do
    {
        k = 0;
        while( (ch = getch()) != '\n' )
        {
            if (ch == ' ' && tab_check == 1)
            {
                getyx(win, y, x);
                clear_complete();
                tab_count = 0;
                getyx(win, ypos, xpos);
            }

            if (ch == '\t')
            {
                if (xpos != -1)
                {
                    xpos = -1;
                    if (mvinch(ypos, xpos+1) != '\n')
                    {
                        move(ypos, xpos+1);
                    }
                    else
                    {
                        continue;
                    }
                }

                if (tab_check == 1){
                    char* dup_op;
                    if ((dup_op = select_word()) != NULL)    
                    {
                        strcpy(tabbed_op, dup_op);
                        /* Replace current word with tabbed_op */
                        int pos = find_space();
                        replace_word(pos, tabbed_op);
                        k = strlen(tabbed_op);

                        /* This is not fixed */
                        if (completion_count == 0)
                            continue;

                        tab_count = (tab_count + 1) % completion_count;
                        continue;
                    }
                    else
                        break;
                }
                else
                    tab_check = 1;

                op[k] = '\0';
                
                getyx(win, y, x);

                /* Clear the previous completion words */
                clear_complete();
                
                completeWord(root, op, op);

                move(y, x);
                clrtoeol();
            }   
            else if (ch == ALT_BACKSPACE)
            {
                /* Check for backspace key (ch == 127) */
                tab_check = 0;
                tab_count = 0;
                backspace();
                xpos = -1;
                if (k > 0)
                {
                    k--;
                }
            }
            else
            {
                tab_check = 0;
                tab_count = 0;
                printw("%c", ch);
                xpos = -1;
                if (ch != ' ')
                    op[k++] = ch;
                else
                    k = 0;
            }       
        }

        getyx(win, y, x);
        clear_complete();
        op[k] = '\0';
        printw("\n");
    }
    while(strcmp(op, "exit") != 0);

    for(int i=0; i<MAX_WORDS; i++)
        free(completion_arr[i]);
    free(completion_arr);

    free(op);
    free(tabbed_op);
    freeNode(root);

    endwin();
    return 0;
}
