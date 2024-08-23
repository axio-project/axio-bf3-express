#include <stddef.h>

#include <libflexio-libc/stdio.h>
#include <libflexio-libc/string.h>
#include <libflexio-dev/flexio_dev.h>
#include <libflexio-dev/flexio_dev_err.h>
#include <libflexio-dev/flexio_dev_queue_access.h>
#include <dpaintrin.h>

__dpa_rpc__ uint64_t
dpa_device_init(uint64_t __unused data){
  return 0;
}

__dpa_global__ void dpa_event_handler(uint64_t __unused arg0){
  if(arg0 == 1){
    goto exit;
  }

exit:
  ;    
}
