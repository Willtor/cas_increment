#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef int cycle_t;
typedef enum inst_t {read,cas} inst_t;
typedef enum cache_t {M, S, I} cache_t;

typedef struct message_t {
  cache_t cache_state;
  cycle_t request_time;
} message_t;

typedef struct state_t {
  inst_t inst_on_deck;
  int read_value;
  int consecutive_successes;
  int total_successes;
  int success_stints;
  int success_squared;
  int consecutive_failures;
  int total_failures;
  int failure_stints;
  int failure_squared;
  cache_t local_cache; 
  message_t maf_entry;
  cycle_t distance_from_llc;
} state_t;

static inline double tdiff(const struct timespec start, const struct timespec end) {
    return end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec)*1e-9;
}

cycle_t global_clock = 1;
int memory_value = 0;
int sparse_flag = 0;

void QuiesceStats(state_t *cores, int N) {
  for (int i = 0; i < N; i++) {
    if (cores[i].consecutive_successes > 0) {
      cores[i].success_stints++;
    }
    if (cores[i].consecutive_failures > 0) {
      cores[i].failure_stints++;
    }
  }
}

void PrintCSV(state_t *cores, int N, int argc, const char *argv[]) {
  for (int i = 1; i < argc; i++) {
    printf("%d,", atoi(argv[i]));
  }
  for (int i = 0; i < N; i++) {
    printf("%d,%d,%d,", cores[i].total_successes, cores[i].success_stints, cores[i].success_squared);
    printf("%d,%d,%d,", cores[i].total_failures, cores[i].failure_stints, cores[i].failure_squared);
    printf("%d", cores[i].distance_from_llc);
    if (i < N-1) printf(",");
  }
  printf("\n");
}

void PrintSummary(state_t *cores, int N) {
  printf("\t\t\t");
  for (int i = 0; i < N; i++) printf("Core %d\t\t", i);
  printf("\n\t\t\t");
  for (int i = 0; i < N; i++) printf("-------\t\t");
  printf("\nTotal Successes/Runs:\t");
  for (int i = 0; i < N; i++) printf("%d/%d\t", cores[i].total_successes, cores[i].success_stints);
  printf("\nmu/sig per run:\t");
  printf("\nTotal Failures/Runs:\t");
  for (int i = 0; i < N; i++) printf("%d/%d\t", cores[i].total_failures, cores[i].failure_stints);
  printf("\n");

}

void PrintHeader(int N) {
  printf("I=instruction, $=cached state (0=M, 1=S, 2=I)\n");
  printf("A=armed value, !=#successes, ?=#failures\n");
  printf("R=request, P=probe, F=fill, A=acknowledge\n\n");
  
  printf("Cycle");
  for (int i = 0; i < N; i++) {
    printf("\t   I,$   ");
    if (sparse_flag == 0)
      printf("A,!,?   ");
    printf("R P A F");
  }
  printf("\n");
}

void PrintState(state_t *core, message_t *fill, message_t *request, message_t *probe, message_t *response, char *str) {
  sprintf(str+strlen(str), "\t   ");
  sprintf(str+strlen(str), "%d,%d   ", (int) core->inst_on_deck, (int) core->local_cache);
    if (sparse_flag == 0)
      sprintf(str+strlen(str), "%d,%d,%d   ", core->read_value % 10, core->consecutive_successes, core->consecutive_failures);
  //sprintf(str+strlen(str), " - %d,", request->request_time);
  //sprintf(str+strlen(str), "%d,", probe->request_time);
  //sprintf(str+strlen(str), "%d,", fill->request_time);
  //sprintf(str+strlen(str), "%d", response->request_time);
  if (request->request_time > 0)
    sprintf(str+strlen(str), "%d ", request->cache_state);
  else
    sprintf(str+strlen(str), ". ");
  if (probe->request_time > 0)
    sprintf(str+strlen(str), "%d ", probe->cache_state);
  else
    sprintf(str+strlen(str), ". ");
  if (response->request_time > 0)
    sprintf(str+strlen(str), "%d ", response->cache_state);
  else
    sprintf(str+strlen(str), ". ");
  if (fill->request_time > 0)
    sprintf(str+strlen(str), "%d ", fill->cache_state);
  else
    sprintf(str+strlen(str), ". ");
}

void HandleProbes(state_t *core, message_t *probe, message_t *response) {
  if (probe->request_time > 0
      && probe->request_time < global_clock) {
    core->local_cache = probe->cache_state;
    response->cache_state = core->local_cache;
    response->request_time = global_clock + core->distance_from_llc;
    probe->request_time = 0;
  } 
}

void HandleFill(state_t *core, message_t *fill) {
  if (fill->request_time > 0
      && fill->request_time < global_clock) {
    core->local_cache = fill->cache_state;
    fill->request_time = 0;
    core->maf_entry.request_time = 0;
  }
}

int IsReadable(cache_t cache_state) {
  if ( (cache_state == M) || (cache_state == S) )
    return 1;
  return 0;
}

int IsWritable(cache_t cache_state) {
  if (cache_state == M) 
    return 1; 
  return 0;
}

void AdvanceInstruction(state_t *core, message_t *request) {
  if (core->maf_entry.request_time == 0) {
    if (core->inst_on_deck == read) {
      if (IsReadable(core->local_cache) == 1) {
        core->read_value = memory_value;
        core->inst_on_deck = cas; // advance pointer...
      } else {
        request->cache_state = S;
        request->request_time = global_clock + core->distance_from_llc;
        core->maf_entry = *request;
      }
    } else {
      if (IsWritable(core->local_cache) == 1) {
        if (core->read_value == memory_value) {
          // success!
          core->consecutive_successes++;
          core->total_successes++;
          if (core->consecutive_failures > 0) {
            core->failure_stints++;
            core->failure_squared += (core->consecutive_failures)*(core->consecutive_failures);
          }
          core->consecutive_failures = 0;
          memory_value = memory_value + 1;
        } else {
          // abject failure...
          core->consecutive_failures++;
          core->total_failures++;
          if (core->consecutive_successes > 0) {
            core->success_stints++;
            core->success_squared += (core->consecutive_successes)*(core->consecutive_successes);
          }
          core->consecutive_successes = 0;
        }
        core->inst_on_deck = read;
      } else {
        request->cache_state = M;
        request->request_time = global_clock + core->distance_from_llc;
        core->maf_entry = *request;
      }
    }
  }
}

void HandleResponse(message_t *response, int *inval_acks) {
  if (response->request_time > 0
      && response->request_time < global_clock) {
    (*inval_acks)--;
    response->request_time = 0;
  }
}

void SendFill(state_t *core, message_t *fill, message_t *request, cache_t *tag_directory) {
  fill->cache_state = request->cache_state;
  fill->request_time = global_clock + core->distance_from_llc;
  request->request_time = 0;
  *tag_directory = fill->cache_state;
}

int GetRequestingCore(message_t *request, int N) {
  int requesting_core = -1;
  int min_request_time = global_clock + 1; // any value greater than a valid request time
  for (int i = 0; i < N; i++) {
    if (request[i].request_time > 0
        && request[i].request_time < global_clock 
        && request[i].request_time < min_request_time) {
      min_request_time = request[i].request_time;
      requesting_core = i;
    }
  }
  return requesting_core;
}

void SendProbe(state_t *core, message_t *request, message_t *probe, cache_t *tag_directory, int *inval_acks) {
  if (request->cache_state == M && *tag_directory != I) {
    probe->cache_state = I;
    probe->request_time = global_clock + core->distance_from_llc;
    *tag_directory = I;
    (*inval_acks)++;
  } else if (request->cache_state == S && *tag_directory == M ) {
    probe->cache_state = S;
    probe->request_time = global_clock + core->distance_from_llc;
    *tag_directory = S;
    (*inval_acks)++;
  }
}

int main (int argc __attribute__((unused)),
          const char *argv[] __attribute__((unused))) {

  if (argc == 1) {
    printf("Usage: %s N L E C D T P\n", argv[0]);
    printf("N = number of threads\n");
    printf("number of cycles between thread n and the shared cache is = ");
    printf("L + C * (n^E)\n");
    printf("D = expected number of cycles that the shared cache delays before accepting a new request after fulfilling the previous one.\n");
    printf("T = total number of cycles to run.\n");
    printf("P = print level (0->cycle by cycle, 1->summary by thread, 2->csv summary)\n");
    return -1;
  }

  const int N = atoi(argv[1]);
  const int min_distance_from_llc = atoi(argv[2]);
  const int exponent = atoi(argv[3]);
  const int constant_multiplier = atoi(argv[4]);
  const int rand_mod = atoi(argv[5]);
  const cycle_t total_clocks = (cycle_t) atoi(argv[6]);
  const int print_level = atoi(argv[7]);

  state_t cores[N];
  cache_t tag_directory[N];
  int inval_acks = 0;

  message_t response[N];
  message_t request[N];
  message_t fill[N];
  message_t probe[N];

  for (int i = 0; i < N; i++) {
    response[i].request_time = 0;
    request[i].request_time = 0;
    fill[i].request_time = 0;
    probe[i].request_time = 0;
    response[i].cache_state = I;
    request[i].cache_state = I;
    fill[i].cache_state = I;
    probe[i].cache_state = I;
    cores[i].inst_on_deck = read;
    cores[i].read_value = 0;
    cores[i].consecutive_successes = 0;
    cores[i].total_successes = 0;
    cores[i].success_stints = 0;
    cores[i].success_squared = 0;
    cores[i].consecutive_failures = 0;
    cores[i].total_failures = 0;
    cores[i].failure_stints = 0;
    cores[i].failure_squared = 0;
    cores[i].local_cache = I;
    cores[i].maf_entry.request_time = 0;
    cores[i].distance_from_llc = constant_multiplier*(i^exponent) + min_distance_from_llc;
    tag_directory[i] = I;
  }
  int requesting_core = -1; // -1 -> no request, otherwise index of requesting core
  char print_string[2][1000*N];
  srand(1043425);
    
  if (print_level == 1) {
    PrintHeader(N);
  }

  for (global_clock = 1; global_clock < total_clocks; global_clock++) {
    for (int i = 0; i < N; i++) {
      HandleProbes(&cores[i], &probe[i], &response[i]);
      HandleFill(&cores[i], &fill[i]);
      AdvanceInstruction(&cores[i], &request[i]);
    }
    if (requesting_core >= 0) {
      if (inval_acks > 0) {
        for (int i = 0; i < N; i++) {
          if (i != requesting_core)
            HandleResponse(&response[i], &inval_acks);
        }
      }
      if (inval_acks == 0) {
        SendFill(&cores[requesting_core], &fill[requesting_core], 
          &request[requesting_core], &tag_directory[requesting_core]);
        requesting_core = -1;
      }
    } else {
      if (rand() % rand_mod == 0) {
        requesting_core = GetRequestingCore(request, N);
        if (requesting_core >= 0) {
          for (int i = 0; i < N; i++) {
            if (i != requesting_core)
              SendProbe(&cores[i], &request[requesting_core], &probe[i], &tag_directory[i], &inval_acks);
          }
  //        if (inval_acks == 0) { // no probes required, clear request and send fill
  //          SendFill(&cores[requesting_core], &fill[requesting_core], 
  //            &request[requesting_core], &tag_directory[requesting_core]);
  //          requesting_core = -1;
  //        }
        }
      }
    }
    if (print_level == 0) {
      print_string[global_clock % 2][0] = 0;
      for (int i = 0; i < N; i++) {
        PrintState(&cores[i], &fill[i], &request[i], &probe[i], &response[i], print_string[global_clock % 2]);
      }
      if (strcmp(print_string[0],print_string[1]) != 0) {
        printf("%d %s\n", global_clock, print_string[global_clock % 2]);
      }
    }
  } 
  if (print_level == 1) {
    QuiesceStats(cores, N);
    PrintSummary(cores, N);
  } else if (print_level == 2) {
    QuiesceStats(cores, N);
    PrintCSV(cores, N, argc, argv);
  }

  return 0;
}
