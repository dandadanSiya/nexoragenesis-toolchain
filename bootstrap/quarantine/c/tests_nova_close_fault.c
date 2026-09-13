/* BOOTSTRAP_C_TEST: close owned streams normally, inject only the reported
 * error on the second close (runtime cleanup after the input source close). */
#include <stdio.h>
int __real_fclose(FILE*);
int __wrap_fclose(FILE*f){static unsigned closes;int result=__real_fclose(f);return ++closes==2?EOF:result;}
