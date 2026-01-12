#include "corvus_synctree_endpoint.h"

// Provide a key function so RTTI/vtables are emitted in a single translation
// unit. The default implementation is empty; derived classes override as needed.
void CorvusTopSynctreeEndpoint::forceSimWorkerReset() {}
