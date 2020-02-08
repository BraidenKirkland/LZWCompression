#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRUE 1
#define FALSE 0

#define DICTSIZE 4096                     /* allow 4096 entries in the dict  */
#define ENTRYSIZE 32

unsigned char dict[DICTSIZE][ENTRYSIZE];  /* of 30 chars max; the first byte */
                                          /* is string length; index 0xFFF   */
                                          /* will be reserved for padding    */
                                          /* the last byte (if necessary)    */

// These are provided below
int read12(FILE *infil);
int write12(FILE *outfil, int int12);
void strip_lzw_ext(char *fname);
void flush12(FILE *outfil);

//Created
void encode(FILE *in, FILE *out);
void add_to_dict(unsigned char new_add[], int len_new_add, int row);
int in_dict(unsigned char array[], int len_array);
void copy_array(unsigned char dest[], unsigned char src[], int len_src);
void reset_dict(void);
void print_entry(int index);

void decode(FILE *in, FILE *out);
void copy_dict_entry(unsigned char array[], int index);
void output_dict_entry(int index, FILE * out);
void output_non_dict_entry(unsigned char entry[], int length, FILE *out);


int main(int argc, char *argv[]) {

    /*****Command line input error checks*****/

   if(argc < 2){

        printf("No input file specified!\n");
        exit(1);
   }

   FILE *input_file = fopen(argv[1], "rb");

   if(input_file == NULL){

       printf("Read error: file not found or cannot be read\n");
       exit(2);
   }

   if(argc < 3 || (strcmp(argv[2], "e") != 0 && strcmp(argv[2], "d") != 0)){

       printf("Invalid Usage, expected: LZW {input_file} [e|d]\n");
       exit(4);
   }

    /*****End of command line input error checks*****/


// Create the standard initial dictionary
reset_dict();


FILE * output_file;

if(*argv[2] == 'e'){

   char output_name[200];
   strncpy(output_name, argv[1], 196);
   strncat(output_name, ".LZW", 10);

   output_file = fopen(output_name, "wb");

   encode(input_file, output_file);

}

if(*argv[2] == 'd'){

   char * contains_extention = strstr(argv[1], ".LZW");

   if(contains_extention == NULL){

      printf("Error: Invalid Format\n");
      exit(3);
   }

   strip_lzw_ext(argv[1]);
   output_file = fopen(argv[1], "wb");
   decode(input_file, output_file);
}

fclose(input_file);
fclose(output_file);

}

// Sets the first 256 ([0,255]) entries of the dict to the standard ascii table 
// Sets the length cell of the remaining entries([256, 4094]) to 0 in order to indicate they are empty
void reset_dict(void){

   int k;
   for(k=0; k < 256; k++){

      dict[k][0] = 1;
      dict[k][1] = k;
   }

   for(k=256; k < 4095; k++){

      dict[k][0] = 0;
   }
}

/*****************************************************************************/
/* encode() performs the Lempel Ziv Welch compression from the algorithm in  */
/* the assignment specification. The strings in the dictionary have to be    */
/* handled carefully since 0 may be a valid character in a string (we can't  */
/* use the standard C string handling functions, since they will interpret   */
/* the 0 as the end of string marker). Again, writing the codes is handled   */
/* by a separate function, just so I don't have to worry about writing 12    */
/* bit numbers inside this algorithm.                                        */
void encode(FILE *in, FILE *out) {
    
    unsigned char w[ENTRYSIZE-1];
    int len_w = 0;

    unsigned char wk[ENTRYSIZE-1];
    int len_wk = 0;

    unsigned char k;
    int row = 256;   // 256 is the starting index of new dict entries
   
    int prev;   // variable for debugging
    int count = 0;
    int current;

    while(fread(&k, 1, 1, in) == 1){
        
       if(len_w != 0){

          copy_array(wk, w, len_w);
          len_wk = len_w;
       }

       wk[len_wk++] = k;
      
       if(in_dict(wk, len_wk) != -1){

          copy_array(w, wk, len_wk);   // Now w = wk
          len_w = len_wk;

       }else{

          write12(out, in_dict(w, len_w));

          prev = current;
          current = in_dict(w, len_w);
          
          if( row < 4095 && len_wk < 32){
              add_to_dict(wk, len_wk, row);
              row++;
          }
          
          len_w = 0;
 
          w[len_w++] = k;

       }
      
       prev = k;
    }

    write12(out, in_dict(w, len_w));

    // Write out remaining partial codes to out file
    write12(out, 4095);
    flush12(out);

    exit(0);
}


/*
   Adds new_add as a new entry to the dictionary located at index row in the dictionary.
   len_new_add is the length of the new addition. It is stored at index 0 of the new entry
*/
void add_to_dict(unsigned char new_add[], int len_new_add, int row){

   dict[row][0] = len_new_add;

   int j;
   for(j=0; j < len_new_add; j++){

      dict[row][j+1] = new_add[j];
   }
}

/*
   Returns the code of the entry [0,4094] in the dictionary if it is already in the dictionary
   Returns -1 if the entry is not in the dictionary
 */
int in_dict(unsigned char array[], int len_array){

   int j;
   for(j=0; j < DICTSIZE; j++){

      if(len_array != dict[j][0]){

         continue;
      }

      int streak = 0;

      int i;
      for(i=0; i < len_array; i++){

         // could break if they are not equal to save time
         if(array[i] == dict[j][i+1]){

            streak++;
         }
      }

      // we already know they are the same length, so if the number of elements
      // in common is the same as the length of the input array, then they must be equal
      if(streak == len_array){

         return j;
      }

   }

   return -1;
}

/*
   Copies the contents of dest in to src.
   Precondition: the arrays have the same max capacity
*/
void copy_array(unsigned char dest[], unsigned char src[], int len_src){

   int j;
   for(j=0; j < len_src; j++){

      char current = src[j];
      dest[j] = src[j];
   }
}


/*****************************************************************************/
/* decode() performs the Lempel Ziv Welch decompression from the algorithm   */
/* in the assignment specification.                                          */
void decode(FILE *in, FILE *out) {

   unsigned char w[ENTRYSIZE-1];
   int len_w = 0;

   unsigned char entry[ENTRYSIZE-1];
   int len_entry = 0;

   // row represents the next available slot in the dictionary 
   // that is available to receive a new entry
   int row = 256;

   int k = read12(in);

   if(k != 0x0FFF){
      output_dict_entry(k, out);
      copy_dict_entry(w, k);
      len_w = dict[k][0];
      k = read12(in);
   }
 
   while(k != 0x0FFF){

      // Copy the contents of w to entry
      copy_array(entry, w, len_w);
      len_entry = len_w;

      if(dict[k][0] != 0){

         output_dict_entry(k, out);
         entry[len_entry++] = dict[k][1];
         
         if(row < 4095 && len_entry < 32){
             
            add_to_dict(entry, len_entry, row);
            row++;
         }
      
      }else{
         
         entry[len_entry++] = w[0];
         
         if(row < 4095 && len_entry < 32){
             
            add_to_dict(entry, len_entry, row);
            row++;
         }
         
         output_non_dict_entry(entry, len_entry, out);  
      }
      
      // Set w to dict[k]
      copy_dict_entry(w, k);

      // Read a code k from the encoded file
      len_w = dict[k][0];

      k = read12(in);
   }

   exit(0);

}


/*
   Writes each character of the string stored at the index 'index' to the 
   outputfile 'out'
*/
void output_dict_entry(int index, FILE * out){

   int j;
   for(j=1; j <= dict[index][0]; j++){
       
      char current = dict[index][j];
      fputc(dict[index][j], out);
   }
}

/*
   Copies the contents of the of the dictionary entry at the index 'index'
   into the array 'array'
*/
void copy_dict_entry(unsigned char array[], int index){

   int j;
   for(j=0; j < dict[index][0]; j++){

      array[j] = dict[index][j+1];
   }
}

/*
   Writes the contents of the array 'entry' to the output file 'out'
   This function is necessary because some sequences need to be outputted even
   though they were not added to the dictionary
*/
void output_non_dict_entry(unsigned char entry[], int length, FILE *out){
    
    int j;
    for(j=0; j < length; j++){
        
        fputc(entry[j], out);
    }
}

/*****************************************************************************/
/* read12() handles the complexities of reading 12 bit numbers from a file.  */
/* It is the simple counterpart of write12(). Like write12(), read12() uses  */
/* static variables. The function reads two 12 bit numbers at a time, but    */
/* only returns one of them. It stores the second in a static variable to be */
/* returned the next time read12() is called.                                */
int read12(FILE *infil)
{
 static int number1 = -1, number2 = -1;
 unsigned char hi8, lo4hi4, lo8;
 int retval;

 if(number2 != -1)                        /* there is a stored number from   */
    {                                     /* last call to read12() so just   */
     retval = number2;                    /* return the number without doing */
     number2 = -1;                        /* any reading                     */
    }
 else                                     /* if there is no number stored    */
    {
     if(fread(&hi8, 1, 1, infil) != 1)    /* read three bytes (2 12 bit nums)*/
        return(-1);
     if(fread(&lo4hi4, 1, 1, infil) != 1)
        return(-1);
     if(fread(&lo8, 1, 1, infil) != 1)
        return(-1);

     number1 = hi8 * 0x10;                /* move hi8 4 bits left            */
     number1 = number1 + (lo4hi4 / 0x10); /* add hi 4 bits of middle byte    */

     number2 = (lo4hi4 % 0x10) * 0x0100;  /* move lo 4 bits of middle byte   */
                                          /* 8 bits to the left              */
     number2 = number2 + lo8;             /* add lo byte                     */

     retval = number1;
    }

 return(retval);
}

/*****************************************************************************/
/* write12() handles the complexities of writing 12 bit numbers to file so I */
/* don't have to mess up the LZW algorithm. It uses "static" variables. In a */
/* C function, if a variable is declared static, it remembers its value from */
/* one call to the next. You could use global variables to do the same thing */
/* but it wouldn't be quite as clean. Here's how the function works: it has  */
/* two static integers: number1 and number2 which are set to -1 if they do   */
/* not contain a number waiting to be written. When the function is called   */
/* with an integer to write, if there are no numbers already waiting to be   */
/* written, it simply stores the number in number1 and returns. If there is  */
/* a number waiting to be written, the function writes out the number that   */
/* is waiting and the new number as two 12 bit numbers (3 bytes total).      */
int write12(FILE *outfil, int int12)
{
 static int number1 = -1, number2 = -1;
 unsigned char hi8, lo4hi4, lo8;
 unsigned long bignum;

 if(number1 == -1)                         /* no numbers waiting             */
    {
     number1 = int12;                      /* save the number for next time  */
     return(0);                            /* actually wrote 0 bytes         */
    }

 if(int12 == -1)                           /* flush the last number and put  */
    number2 = 0x0FFF;                      /* padding at end                 */
 else
    number2 = int12;

 bignum = number1 * 0x1000;                /* move number1 12 bits left      */
 bignum = bignum + number2;                /* put number2 in lower 12 bits   */

 hi8 = (unsigned char) (bignum / 0x10000);                     /* bits 16-23 */
 lo4hi4 = (unsigned char) ((bignum % 0x10000) / 0x0100);       /* bits  8-15 */
 lo8 = (unsigned char) (bignum % 0x0100);                      /* bits  0-7  */

 fwrite(&hi8, 1, 1, outfil);               /* write the bytes one at a time  */
 fwrite(&lo4hi4, 1, 1, outfil);
 fwrite(&lo8, 1, 1, outfil);

 number1 = -1;                             /* no bytes waiting any more      */
 number2 = -1;

 return(3);                                /* wrote 3 bytes                  */
}

/** Write out the remaining partial codes */
void flush12(FILE *outfil)
{
 write12(outfil, -1);                      /* -1 tells write12() to write    */
}                                          /* the number in waiting          */

/** Remove the ".LZW" extension from a filename */
void strip_lzw_ext(char *fname)
{
    char *end = fname + strlen(fname);

    while (end > fname && *end != '.' && *end != '\\' && *end != '/') {
        --end;
    }
    if ((end > fname && *end == '.') &&
        (*(end - 1) != '\\' && *(end - 1) != '/')) {
        *end = '\0';
    }
}








