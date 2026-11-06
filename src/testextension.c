/* This code is to test extension loading and is taken from
   https://sqlite.org/cvstrac/wiki/wiki?p=LoadableExtensions */

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

/*
    ** The half() SQL function returns half of its input value.
    */
static void
halfFunc(sqlite3_context *context, int argc, sqlite3_value **argv)
{
  sqlite3_result_double(context, 0.5 * sqlite3_value_double(argv[0]));
}

/* SQLite invokes this routine once when it loads the extension.
    ** Create new functions, collating sequences, and virtual table
    ** modules here.  This is usually the only exported symbol in
    ** the shared library.
    */
#ifdef _WIN32
__declspec(dllexport)
#endif
int
sqlite3_extension_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
{
  SQLITE_EXTENSION_INIT2(pApi)
  return sqlite3_create_function_v2(db, "half", 1, SQLITE_ANY, 0, halfFunc, 0, 0, 0);
}

/* this is code added by me and checks that alternate entry points work by
   providing an double function */

static void
doubleFunc(sqlite3_context *context, int argc, sqlite3_value **argv)
{
  sqlite3_result_double(context, 2.0 * sqlite3_value_double(argv[0]));
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int
alternate_sqlite3_extension_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
{
  SQLITE_EXTENSION_INIT2(pApi)
  return sqlite3_create_function_v2(db, "doubleup", 1, SQLITE_ANY, 0, doubleFunc, 0, 0, 0);
}

/* A VFS named "slowsync" that wraps the OS VFS and sleeps in C before
   each xSync, for the number of milliseconds in the sync_delay_ms URI
   parameter.  Tests use it to make SQLite block on "disk" with no Python
   code running, so they can check that APSW released the GIL. */

typedef struct
{
  sqlite3_file base;
  sqlite3_file *real; /* allocated directly after this struct */
  int delay_ms;
  /* slow_io_methods with the iVersion of the real file, so SQLite does
     not call methods the real file lacks */
  sqlite3_io_methods methods;
} SlowFile;

static sqlite3_vfs *slow_real_vfs;
static sqlite3_vfs slow_vfs;

#define REAL(f) (((SlowFile *)(f))->real)

static int
slowClose(sqlite3_file *f)
{
  return REAL(f)->pMethods->xClose(REAL(f));
}

static int
slowRead(sqlite3_file *f, void *buf, int amt, sqlite3_int64 ofst)
{
  return REAL(f)->pMethods->xRead(REAL(f), buf, amt, ofst);
}

static int
slowWrite(sqlite3_file *f, const void *buf, int amt, sqlite3_int64 ofst)
{
  return REAL(f)->pMethods->xWrite(REAL(f), buf, amt, ofst);
}

static int
slowTruncate(sqlite3_file *f, sqlite3_int64 size)
{
  return REAL(f)->pMethods->xTruncate(REAL(f), size);
}

static int
slowSync(sqlite3_file *f, int flags)
{
  if (((SlowFile *)f)->delay_ms)
    sqlite3_sleep(((SlowFile *)f)->delay_ms);
  return REAL(f)->pMethods->xSync(REAL(f), flags);
}

static int
slowFileSize(sqlite3_file *f, sqlite3_int64 *size)
{
  return REAL(f)->pMethods->xFileSize(REAL(f), size);
}

static int
slowLock(sqlite3_file *f, int level)
{
  return REAL(f)->pMethods->xLock(REAL(f), level);
}

static int
slowUnlock(sqlite3_file *f, int level)
{
  return REAL(f)->pMethods->xUnlock(REAL(f), level);
}

static int
slowCheckReservedLock(sqlite3_file *f, int *out)
{
  return REAL(f)->pMethods->xCheckReservedLock(REAL(f), out);
}

static int
slowFileControl(sqlite3_file *f, int op, void *arg)
{
  return REAL(f)->pMethods->xFileControl(REAL(f), op, arg);
}

static int
slowSectorSize(sqlite3_file *f)
{
  return REAL(f)->pMethods->xSectorSize(REAL(f));
}

static int
slowDeviceCharacteristics(sqlite3_file *f)
{
  return REAL(f)->pMethods->xDeviceCharacteristics(REAL(f));
}

static int
slowShmMap(sqlite3_file *f, int region, int size, int extend, void volatile **out)
{
  return REAL(f)->pMethods->xShmMap(REAL(f), region, size, extend, out);
}

static int
slowShmLock(sqlite3_file *f, int offset, int n, int flags)
{
  return REAL(f)->pMethods->xShmLock(REAL(f), offset, n, flags);
}

static void
slowShmBarrier(sqlite3_file *f)
{
  REAL(f)->pMethods->xShmBarrier(REAL(f));
}

static int
slowShmUnmap(sqlite3_file *f, int delete_flag)
{
  return REAL(f)->pMethods->xShmUnmap(REAL(f), delete_flag);
}

static int
slowFetch(sqlite3_file *f, sqlite3_int64 ofst, int amt, void **out)
{
  return REAL(f)->pMethods->xFetch(REAL(f), ofst, amt, out);
}

static int
slowUnfetch(sqlite3_file *f, sqlite3_int64 ofst, void *p)
{
  return REAL(f)->pMethods->xUnfetch(REAL(f), ofst, p);
}

static const sqlite3_io_methods slow_io_methods = {
  3,
  slowClose,
  slowRead,
  slowWrite,
  slowTruncate,
  slowSync,
  slowFileSize,
  slowLock,
  slowUnlock,
  slowCheckReservedLock,
  slowFileControl,
  slowSectorSize,
  slowDeviceCharacteristics,
  slowShmMap,
  slowShmLock,
  slowShmBarrier,
  slowShmUnmap,
  slowFetch,
  slowUnfetch,
};

static int
slowOpen(sqlite3_vfs *vfs, sqlite3_filename name, sqlite3_file *f, int flags, int *out_flags)
{
  SlowFile *p = (SlowFile *)f;
  int res;

  p->real = (sqlite3_file *)&p[1];
  p->delay_ms = name ? (int)sqlite3_uri_int64(name, "sync_delay_ms", 0) : 0;
  res = slow_real_vfs->xOpen(slow_real_vfs, name, p->real, flags, out_flags);
  /* SQLite calls xClose only when pMethods is set */
  p->base.pMethods = 0;
  if (p->real->pMethods)
  {
    p->methods = slow_io_methods;
    p->methods.iVersion = p->real->pMethods->iVersion;
    p->base.pMethods = &p->methods;
  }
  return res;
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int
slowsync_sqlite3_extension_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
{
  int res;

  SQLITE_EXTENSION_INIT2(pApi)
  if (!slow_real_vfs)
  {
    /* the OS VFS by name, because a test may have made a Python VFS the
       default, and that one can be unregistered later */
    slow_real_vfs = sqlite3_vfs_find("unix");
    if (!slow_real_vfs)
      slow_real_vfs = sqlite3_vfs_find("win32");
    if (!slow_real_vfs)
      slow_real_vfs = sqlite3_vfs_find(0);
    if (!slow_real_vfs)
      return SQLITE_ERROR;
    /* every method except xOpen goes straight to the OS VFS, and the copy
       keeps its pAppData */
    slow_vfs = *slow_real_vfs;
    slow_vfs.pNext = 0;
    slow_vfs.zName = "slowsync";
    slow_vfs.szOsFile = (int)sizeof(SlowFile) + slow_real_vfs->szOsFile;
    slow_vfs.xOpen = slowOpen;
  }
  res = sqlite3_vfs_register(&slow_vfs, 0);
  /* the VFS outlives this connection, so the library must stay loaded */
  return res == SQLITE_OK ? SQLITE_OK_LOAD_PERMANENTLY : res;
}
