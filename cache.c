#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <cilk/cilk.h>

void cache1(int *restrict a, size_t N, size_t iterations) {
  for (size_t j = 0; j < iterations; j++) {  
    for (size_t i = 0; i < N; i++)
      a[i] = a[i] + 5;
  }
}

void cache2(int *restrict a, int *restrict b, 
           int *restrict c, size_t N, size_t iterations) {
  for (size_t j = 0; j < iterations; j++ ) {
    for (size_t i = 0; i < N; i++)
      c[i] = a[i] + b[i] + a[N-i-1] + b[N-i-1] + c[N-i-1];
  }
}

void cache3(int *restrict a, size_t N, size_t mask, size_t iterations) {
  for (size_t j = 0; j < iterations; j++) {
    for (size_t i = 0; i < N; i++) {
      int addr = ((i + 523)*253573) & mask;
      a[addr] = a[addr] + 5;
    }
  }
}

void cache4(int *restrict a, size_t N, size_t iterations) {
  for (size_t j = 0; j < iterations; j++ ) {
    cilk_for (size_t i = 0; i < N; i++)
      a[i] = a[i] + 5;
  }
}

void cache5(int *restrict a, int *restrict b, 
           int *restrict c, size_t N, size_t iterations) {
  for (size_t j = 0; j < iterations; j++ ) {
    cilk_for (size_t i = 0; i < N; i++)
      c[i] = a[i] + b[i] + a[N-i-1] + b[N-i-1] + c[N-i-1];
  }
}

void cache6(int *restrict a, size_t N, size_t mask, size_t iterations) {
  for (size_t j = 0; j < iterations; j++) {
    cilk_for (size_t i = 0; i < N; i++) {
      int addr = ((i + 523)*253573) & mask;
      a[addr] = a[addr] + 5;
    }
  }
}

static inline double tdiff(const struct timespec start, const struct timespec end) {
    return end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec)*1e-9;
}

static void clear_caches(int *e, size_t N) {
  struct timespec start, end;

  printf("Clearing caches...\n");
  clock_gettime(CLOCK_MONOTONIC, &start);
  cilk_for (size_t i = 0; i < N; ++i) {
    e[i] *= e[i];
  }
  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Done.  Time: %0.6f\n", tdiff(start, end));
}

int main (int argc __attribute__((unused)),
          const char *argv[] __attribute__((unused))) {

  const size_t min_log = 10;
  const size_t max_log = 24;
  const size_t n = 1 << max_log;
  struct timespec start, end;

  printf("Allocating...");
  clock_gettime(CLOCK_MONOTONIC, &start);
  double **stats = (double **) malloc((max_log+1) * sizeof(double *));
  for (size_t i = 0; i <= max_log; i++)
    stats[i] = calloc(sizeof(double), 6);
  int *A = malloc(n * sizeof(*A));
  int *B = malloc(n * sizeof(*B));
  int *C = malloc(n * sizeof(*C));
  int *E = malloc(n * sizeof(*E));
  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Done.  Time: %0.6f\n", tdiff(start, end));

  printf("Initializing...\n");
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (size_t i = 0; i < n; ++i) {
    A[i] = rand() % RAND_MAX;
    B[i] = rand() % RAND_MAX;
    C[i] = rand() % RAND_MAX;
    E[i] = rand() % RAND_MAX;
  }
  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Done.  Time: %0.6f\n", tdiff(start, end));

  for (size_t n_log = min_log; n_log <= max_log; n_log++) { 
    printf("\n\n n_log = %d \n\n", (int) n_log);
    size_t iterations = 1 << (max_log - n_log + 4);
    size_t n2 = 1 << n_log;
    size_t mask = (1 << n_log) - 1;

    clear_caches(E, 1 << 22);

    printf("Computing frob1...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    cache1(A, n2, iterations);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("done.  Running time for frob1: %0.6f\n", tdiff(start, end));
    stats[n_log][0] += tdiff(start, end);

    clear_caches(E, 1 << 22);

    printf("Computing frob2...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    cache2(A, B, C, n2, iterations >> 2);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("Done.  Running time for frob2: %0.6f\n", tdiff(start, end));
    stats[n_log][1] += tdiff(start, end);


    printf("Computing frob3...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    cache3(A, n2, mask, iterations >> 2);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("Done.  Running time for frob3: %0.6f\n", tdiff(start, end));
    stats[n_log][2] += tdiff(start, end);

    clear_caches(E, 1 << 22);

    printf("Computing frob4...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    cache4(A, n2, iterations);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("Done.  Running time for frob4: %0.6f\n", tdiff(start, end));
    stats[n_log][3] += tdiff(start, end);

    clear_caches(E, 1 << 22);

    printf("Computing frob5...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    cache5(A, B, C, n2, iterations >> 2);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("Done.  Running time for frob5: %0.6f\n", tdiff(start, end));
    stats[n_log][4] += tdiff(start, end);

    clear_caches(E, 1 << 22);

    printf("Computing frob6...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    cache6(A, n2, mask, iterations >> 2);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("Done.  Running time for frob5: %0.6f\n", tdiff(start, end));
    stats[n_log][5] += tdiff(start, end);

  }
  for (size_t n_log = min_log; n_log <= max_log; n_log++) {
    printf("%d, %.06f, %0.6f, %0.6f, %0.6f, %0.6f, %0.6f\n", (int) n_log, stats[n_log][0], stats[n_log][1], stats[n_log][2], stats[n_log][3], stats[n_log][4], stats[n_log][5]);
  }

  return 0;
}
