#include<iostream>
#include<cstdlib>
#include<cmath>
#include<string>
#include<algorithm>
#include<cstdio>
#include "lodepng.h"


unsigned char *inputImage;
unsigned int imageWidth, imageHeight;


int main(){
    // ios_base::sync_with_stdio(false);
    lodepng_decode32_file(&inputImage, &imageWidth, &imageHeight, "source.png");
    std::cout << imageHeight << " " << imageWidth << std::endl;
    system("pause");
}