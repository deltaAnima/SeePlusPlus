#include <iostream>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <immintrin.h>

const int SIZE = 15;

int findSmallestErr(uint8_t* median, uint8_t* arr[15], int resolution){
  int smallesIndex;
  double smallestErr = 1e18;
  uint8_t *m = median;
  uint8_t *curr = arr[0];
  double L1 = 0;
  for(int i = 0; i<SIZE*resolution; i++){
    if(i!=0&&i%5==0){
      if(smallestErr>L1){
        smallestErr = L1;
        smallesIndex = i/5;
      }
      L1 = 0;
      m = median;
      curr = arr[i/5];
    }
    L1+=*m-*curr;

    m++;
    curr++;
  }
  return smallesIndex;
}
