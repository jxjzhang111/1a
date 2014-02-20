// UCLA CS 111 Lab 1 main program

#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include "command.h"
#include "command-internals.h"
#include "alloc.h"
#include "strmap.h"

#define DEBUG 1
#define WORDMIN 2
#define MAX_PROC_COUNT 1000
#define HASHTABLE_SIZE 200
#define SCAN_INTERVAL 100000 // us

static char const *program_name;
static char const *script_name;

static void
usage (void)
{
  error (1, 0, "usage: %s [-pto] SCRIPT-FILE", program_name);
}

static int
get_next_byte (void *stream)
{
  return getc (stream);
}

// typedefs
typedef struct graph_node {
  command_t command;
  int seq_no;
  char **inputs;
  char **outputs;
  struct graph_node **out_edges;
  int max_edge_count;
  int edge_count;
  int in_edges;
} graph_node;

typedef struct graph_nodes {
  graph_node *node;
  struct graph_nodes *next;
  struct graph_nodes *prev;
} graph_nodes;

typedef struct Node {
    struct Node* next;
    int value;
} Node;

// TODO: split up disconnected graphs and run independently
typedef struct graph_list {
  graph_nodes *graph;
  struct graph_list *next;
} graph_list;

typedef struct child_node {
  pid_t child;
  graph_node *node;
  struct child_node *next;
} child_node;

// functions
graph_node *parse_io (command_t command, int command_number);
char **extract_io (command_t command, char io);
int contains (char *w, char **words);
int intersect (char **words1, char **words2);
void add_edge (graph_node *src, graph_node *dst);
command_t execute_parallel (graph_nodes *node_list, int time_travel);
void decrement (graph_node *node);
graph_nodes *append_command (graph_nodes *last_node, command_t command, int *command_number);

// overload protection functions
void scan_and_kill();
void list_push(Node** head, int val);
int list_pop(Node** head);
int get_proc_key(struct dirent *dir, char *key);
int allDigits(char *);

int
main (int argc, char **argv)
{
  int opt;
  int command_number = 1;
  int print_tree = 0;
  int time_travel = 0;
  int overload_protection = 0;
  int pid;
  program_name = argv[0];

  for (;;)
    switch (getopt (argc, argv, "pto"))
  {
    case 'p': print_tree = 1; break;
    case 't': time_travel = 1; break;
    case 'o': overload_protection = 1; break;
    default: usage (); break;
    case -1: goto options_exhausted;
  }
  options_exhausted:;

    // There must be exactly one file argument.
  if (optind != argc - 1)
    usage ();

  script_name = argv[optind];
  FILE *script_stream = fopen (script_name, "r");
  if (! script_stream)
    error (1, errno, "%s: cannot open", script_name);
  command_stream_t command_stream =
  make_command_stream (get_next_byte, script_stream);

  command_t last_command = NULL;
  command_t command;

  if (print_tree) {
    while ((command = read_command_stream (command_stream))) {
      printf ("# %d\n", command_number++);
      print_command (command);
    }
  } else {
    if(overload_protection) {
      //start process overload protection
      if(DEBUG) printf("Overload Protection On!\n");

      pid = fork();
      if(pid == 0) {  // child process do the monitoring
        if(setpriority(PRIO_PROCESS, getpid(), -20) < 0) { // set to highest priority
          printf("Cannot raise priority! Monitor process exit.\n");
          exit(1);
        }
        while(1) {
          scan_and_kill();
          usleep(SCAN_INTERVAL);   // scan the process table every second
        }
      }
    }

    if(!time_travel) {
      while ((command = read_command_stream (command_stream))) {
        last_command = command;
        execute_command (command, time_travel);
      }
    } else {
      if (DEBUG) printf("Commencing time travel\n");
      graph_nodes *node_list = (graph_nodes *) checked_malloc (sizeof (graph_nodes));
      graph_nodes *last_node = node_list;
      node_list->prev = NULL;
      node_list->node = NULL;

      // Parse out input/outputs; create list of graph_nodes (node_list)
      while ((command = read_command_stream (command_stream))) {
        last_node = append_command (last_node, command, &command_number);
      }

      // Fill out dependency edges in node_list
      last_node = node_list;
      graph_nodes *prev_node = node_list;
      while (last_node) {
        while (prev_node->node->seq_no < last_node->node->seq_no) {
          if (intersect (last_node->node->outputs, prev_node->node->inputs)
              || intersect (last_node->node->inputs, prev_node->node->outputs)
              || intersect (last_node->node->outputs, prev_node->node->outputs)) { // Avoid interleaving output files
            add_edge (prev_node->node, last_node->node);
          }
          prev_node = prev_node->next;
        }
        prev_node = node_list;
        last_node = last_node->next;
      }

      // TODO: split up disconnected graphs and run separately

      // Execute the graph_nodes
      last_command = execute_parallel (node_list, time_travel);
    }
  }

  if(overload_protection) 
    kill(pid, SIGKILL);  // kill the monitoring child process

  return print_tree || !last_command ? 0 : command_status (last_command);
}

// Allocates a graph_node instance that points to command and holds dependency info
graph_node *parse_io (command_t command, int command_number) {
  graph_node *node = (graph_node *) checked_malloc (sizeof (graph_node));
  node->command = command;
  node->seq_no = command_number;
  node->outputs = extract_io (command, 'o');
  node->inputs = extract_io (command, 'i');
  node->in_edges = 0;
  node->max_edge_count = 0;
  node->edge_count = 0;
  node->out_edges = NULL;

  if (DEBUG) {
    printf ("\n\toutputs: ");
    char **w = node->outputs;
    while (*w) {
      printf("%s ", *w);
      *w++;
    }
    printf ("\n\tinputs: ");
    w = node->inputs;
    while (*w) {
      printf("%s ", *w);
      *w++;
    }
    printf ("\n");
  }
  return node;
}

// Returns list of words from command that are classified as input/output ('i' or 'o')
char **extract_io (command_t command, char io) {
  size_t max_word_count = WORDMIN;
  size_t max_size = max_word_count * sizeof (char *);
  size_t word_count = 0;
  char **words = (char **) checked_malloc (max_size);
  char **words1 = NULL, **words2 = NULL;

  if (command->type == SIMPLE_COMMAND) {
    if (io == 'o') {
      if (command->output) {
        words[word_count] = command->output;
        word_count++;
      }
    } else if (io == 'i') {
      if (command->input) {
        words[word_count] = command->input;
        word_count++;
        words[word_count] = 0;
      }

      char **w = command->u.word;
      while (*w && *w[0] != '-' && !contains(*w, words)) {
        words[word_count] = *w;
        word_count++;
        if (word_count == max_word_count) {
          words = checked_grow_alloc (words, &max_size);
          max_word_count = max_size / (sizeof (char *));
        }
        words[word_count] = 0;
        *++w;
      }
    }
  } else if (command->type == SUBSHELL_COMMAND) { // Note: not supporting redirects after subshells
    words1 = extract_io (command->u.subshell_command, io);
  } else {
    words1 = extract_io (command->u.command[0], io);
    words2 = extract_io (command->u.command[1], io);
  }
  
  words[word_count] = 0;
  
  // append words1/2 to words while skipping repeats
  char **w;
  if (words1) {
    w = words1;
    while (*w) {
      if (!contains(*w, words)) {
        words[word_count] = *w;
        word_count++;
        if (word_count == max_word_count) {
          words = checked_grow_alloc (words, &max_size);
          max_word_count = max_size / (sizeof (char *));
        }
        words[word_count] = 0;
      }
      *w++;
    }
    free (words1);
  }
  words[word_count] = 0;

  if (words2) {
    w = words2;

    while (*w) {
      if (!contains(*w, words)) {
        words[word_count] = *w;
        word_count++;
        if (word_count == max_word_count) {
          words = checked_grow_alloc (words, &max_size);
          max_word_count = max_size / (sizeof (char *));
        }
        words[word_count] = 0;
      }
      *w++;
    }
    free (words2);
  }
  words[word_count] = 0;
  return words;
}

// Returns 1 if words1 and words2 intersect
int intersect (char **words1, char **words2) {
  while (*words1) {
    if (contains (*words1, words2)) {
      return 1;
    }
    *words1++;
  }
  return 0;
}

// Returns 1 if words contains w
int contains (char *w, char **words) {
  while (*words) {
    if (!strcmp (w, *words)) {
      return 1;
    }
    *words++;
  }
  return 0;
}

void add_edge (graph_node *src, graph_node *dst) {
  if (DEBUG) printf ("Adding edge from %i to %i\n", src->seq_no, dst->seq_no);
  if (!src->out_edges) {
    src->max_edge_count = WORDMIN;
    src->out_edges = (graph_node **) checked_malloc (sizeof (graph_node *) * src->max_edge_count);
  }
  src->out_edges[src->edge_count] = dst;
  src->edge_count++;
  if (src->edge_count == src->max_edge_count) {
    size_t max_size = src->max_edge_count * sizeof (graph_node *);
    src->out_edges = checked_grow_alloc (src->out_edges, &max_size);
    src->max_edge_count = max_size / (sizeof (graph_node *));
  }
  src->out_edges[src->edge_count] = 0;
  dst->in_edges++;
}

command_t execute_parallel (graph_nodes *node_list, int time_travel) {
  command_t last_command = node_list->node->command;
  graph_nodes *current_node = node_list;
  child_node *children = NULL;
  child_node *last_child = children;
  pid_t child;
  int status;

  // Find disconnected graphs and separate into graphs
  // Execute each graph separately (fork)
  // Grandparent: wait for all graphs to complete
  
  while (node_list || children) {
      // Parent: find nodes with no incoming edges and run separately (fork)
    while (current_node) {
      if (DEBUG) printf ("Pre-execution: %i (%i)\n", current_node->node->seq_no, current_node->node->in_edges);
      if (current_node->node->in_edges == 0) {
        child = fork();
        if (child == 0) { // child
          if (DEBUG) printf ("Executing command %i... ", current_node->node->seq_no);
          execute_command (current_node->node->command, time_travel);
          if (DEBUG) printf ("complete [%i]\n", current_node->node->command->status);
          exit(current_node->node->command->status);
        } else if (child > 0) { // parent
          // append new child process to children
          child_node *new_child = (child_node *) malloc (sizeof (child_node *));
          new_child->node = current_node->node;
          new_child->child = child;
          new_child->next = NULL;

          if (last_child)
            last_child->next = new_child;
          last_child = new_child;

          if (!children)
            children = last_child;

          // Prune from node_list
          if (DEBUG) printf ("Pruning %i from node list; ", current_node->node->seq_no);
          graph_nodes *executing_node = current_node;
          if (current_node == node_list) { // special case for if the completed node is first in nodelist
            if (DEBUG) printf ("moving node_list forward\n");
            node_list = node_list->next;
            if (node_list)
              node_list->prev = NULL;
            current_node = node_list;
          } else if (current_node->prev) {
            if (DEBUG) printf ("excising element\n");
            current_node->prev->next = current_node->next;
            current_node->next->prev = current_node->prev;
            current_node = current_node->next;
          }
          free (executing_node);
        } else
          error (1, 0, "execute_parallel: failed to create child process!");
      } else {
        current_node = current_node->next;
      }
    }

    if (DEBUG) printf ("Traversed! ");

    // Parent waitpid for children
    if (DEBUG) printf("\nWaiting for %i:%i to complete...", children->node->seq_no, children->child);
    waitpid (children->child, &status, 0);
    if (DEBUG) printf(" completed with status %i\n", status);
    child_node *completed_child = children;
    int complete = children->node->seq_no;

    // Parent: prune completed child nodes from nodelist; decrement in_edges
    children = children->next;
    decrement (completed_child->node);
    if (!children)
      last_child = NULL;

    last_command = completed_child->node->command;
    free (completed_child->node);
    free (completed_child);
    current_node = node_list;
    // TODO: (recursive) Find disconnected graphs, and execute separately
  }

  return last_command;
}

void decrement (graph_node *node) {
  graph_node **out = node->out_edges;
  while (out && *out) {
    if (DEBUG) printf ("\nDecrementing from %i; ", (*out)->seq_no);
    (*out)->in_edges--;
    *out++;
  }
}


graph_nodes *append_command (graph_nodes *last_node, command_t command, int *command_number) {
  // Split up top level sequence commands
  while (command->type == SEQUENCE_COMMAND) {
    if (command->type == SEQUENCE_COMMAND) {
      last_node = append_command (last_node, command->u.command[0], command_number);
      last_node = append_command (last_node, command->u.command[1], command_number);
      free (command);
      return last_node;
    }
  }
  // Parse and add to last_node
  if (DEBUG) {
    printf ("# %d\n", *command_number);
    print_command (command);
  }

  graph_node *n = parse_io (command, (*command_number)++);
  if (last_node->node) {
    last_node->next = (graph_nodes *) checked_malloc (sizeof (graph_nodes));
    last_node->next->prev = last_node;
    last_node = last_node->next;
  }
  last_node->node = n;
  last_node->next = NULL;
  return last_node;
}

void scan_and_kill() {
  DIR *proc = NULL;
    
  if ((proc = opendir ("/proc/")) == NULL) {
    printf ("\nERROR! pdir could not be initialised correctly");
    return;
  }

  struct dirent *dir = NULL;

  StrMap *sm = sm_new(HASHTABLE_SIZE);
  int bomb_found = -1;
  char bomb_maker[100] = {'\0'};

  if (sm == NULL) {
    printf ("\nERROR! Could not initialize hashtable");
    return;
  }

  while ((dir = readdir (proc)) != NULL)
  {   
    // search and process all processes in /proc/
    if(allDigits(dir->d_name) == 1) {
      int count = 1;
      char key[100] = {'\0'};

      if(get_proc_key(dir, key) < 0){
        printf("Can't generate process key: %s\n", dir->d_name);
        continue;
      }

      if(sm_exists(sm, key)) {
        sm_get(sm, key, &count);
        count++;
      }
      sm_put(sm, key, count);

      if(count > MAX_PROC_COUNT) {
        // found a trouble maker here
        strcpy(bomb_maker, key);   // record the process key
        if(DEBUG) printf("Bomb found: %s\n", bomb_maker);
        bomb_found = 1;
        break;
      }
    }
  }
  
  /* When done, destroy the StrMap object */
  sm_delete(sm);

  if(bomb_found > 0) {
    int kill_count = 0;
    int prev_kill_count;
    Node *kill_stack = NULL;

    do {
      rewinddir(proc);
      prev_kill_count = kill_count;
      kill_count = 0;
      while ((dir = readdir (proc)) != NULL) {
        if(allDigits(dir->d_name) == 1) {
          char key[100] = {'\0'};

          if(get_proc_key(dir, key) < 0){
            printf("Can't generate process key: %s\n", dir->d_name);
            continue;
          }

          if(strcmp(key, bomb_maker) == 0) {
            int pid = atoi(dir->d_name);
            list_push(&kill_stack, pid);
            kill(pid, SIGSTOP);
            kill_count++;
          }
        }
      }
    } while(prev_kill_count != kill_count);

    int pid;
    while((pid = list_pop(&kill_stack)) != -1) {
      kill(pid, SIGKILL);
    }
    if(DEBUG) printf("All process killed.\n");
  }

  closedir (proc);
}

void list_push(Node** head, int val) {
  Node *new_head = (Node *)malloc(sizeof(Node));
  new_head->value = val;
  new_head->next = *head;
  *head = new_head;
}

int list_pop(Node** head) {
  if(*head == NULL)
    return -1;
  int val = (*head)->value;
  Node* tmp = *head;
  *head = (*head)->next;
  free(tmp);
  return val;
}

int get_proc_key(struct dirent *dir, char *key) {
  char dir_name[50] = {'\0'};
  char cmd_name[50] = {'\0'};
  struct stat d_stat;
  FILE *fp;
  
  sprintf(dir_name, "%s/%s", "/proc/", dir->d_name);
  if( stat(dir_name, &d_stat) != 0) {
    printf( "Unable to read directory status." );
    return -1;
  }  // get directory status (UID)
  
  strcat (dir_name, "/status");

  if ( (fp = fopen(dir_name, "r")) == NULL ) {
    printf( "Unable to read directory file." );
    return -1;
  }
  fscanf(fp, "%s %s", cmd_name, cmd_name);  // get process name
  fclose(fp);

  // create a process identifier
  sprintf(key, "%s-%d", cmd_name, (int)d_stat.st_uid);
  return 0;
}

int allDigits(char * in) {
  int i = 0;

  while(in[i] != '\0') {
    if(in[i] < 48 || in[i] > 57)
      return 0;
    i++;
  }

  return 1;
}