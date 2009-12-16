/*
* $Id:  $
* $Version: $
*
* Copyright (c) Priit J�rv 2009
*
* This file is part of wgandalf
*
* Wgandalf is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* 
* Wgandalf is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with Wgandalf.  If not, see <http://www.gnu.org/licenses/>.
*
*/

 /** @file wgdbmodule.c
 *  Python extension module for accessing wgandalf database
 *
 */

/* ====== Includes =============== */

#include <Python.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "dbapi.h"

/* ====== Private headers and defs ======== */

typedef struct {
  PyObject_HEAD
  void *db;
} wg_database;

typedef struct {
  PyObject_HEAD
  void *rec;
} wg_record;

/* Helper macros. Note that the Python types referenced
 * are defined later */

#define VALIDATE_DB(x) \
if(!PyObject_TypeCheck(x, &wg_database_type)) {\
  PyErr_SetString(PyExc_TypeError,\
    "Argument must be a WGandalf database object.");\
  return NULL;\
}

#define VALIDATE_REC(x) \
if(!PyObject_TypeCheck(x, &wg_record_type)) {\
  PyErr_SetString(PyExc_TypeError,\
    "Argument must be a WGandalf db record object.");\
  return NULL;\
}

/* ======= Private protos ================ */

static PyObject *wgdb_attach_database(PyObject *self, PyObject *args,
                                        PyObject *kwds);
static PyObject *wgdb_delete_database(PyObject *self, PyObject *args);
static PyObject *wgdb_create_record(PyObject *self, PyObject *args);
static PyObject *wgdb_get_first_record(PyObject *self, PyObject *args);
static PyObject *wgdb_get_next_record(PyObject *self, PyObject *args);
static PyObject *wgdb_get_record_len(PyObject *self, PyObject *args);

static PyObject *wgdb_set_null_field(PyObject *self, PyObject *args);
static PyObject *wgdb_set_int_field(PyObject *self, PyObject *args);
static PyObject *wgdb_set_double_field(PyObject *self, PyObject *args);
static PyObject *wgdb_set_str_field(PyObject *self, PyObject *args);
static PyObject *wgdb_get_decoded_field(PyObject *self, PyObject *args);

static PyObject *wg_database_repr(wg_database *obj);
static PyObject *wg_record_repr(wg_record *obj);

/* ============= Private vars ============ */

/** Module exception object */
static PyObject *wgdb_error;

/** Database object type */
static PyTypeObject wg_database_type = {
  PyObject_HEAD_INIT(NULL)
  0,                            /*ob_size*/
  "wgdb.Database",              /*tp_name*/
  sizeof(wg_database),          /*tp_basicsize*/
  0,                            /*tp_itemsize*/
  0,                            /*tp_dealloc*/
  0,                            /*tp_print*/
  0,                            /*tp_getattr*/
  0,                            /*tp_setattr*/
  0,                            /*tp_compare*/
  (reprfunc) wg_database_repr,  /*tp_repr*/
  0,                            /*tp_as_number*/
  0,                            /*tp_as_sequence*/
  0,                            /*tp_as_mapping*/
  0,                            /*tp_hash */
  0,                            /*tp_call*/
  (reprfunc) wg_database_repr,  /*tp_str*/
  0,                            /*tp_getattro*/
  0,                            /*tp_setattro*/
  0,                            /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT,           /*tp_flags*/
  "WGandalf database object",   /* tp_doc */
};

/** Record object type */
static PyTypeObject wg_record_type = {
  PyObject_HEAD_INIT(NULL)
  0,                            /*ob_size*/
  "wgdb.Record",                /*tp_name*/
  sizeof(wg_record),            /*tp_basicsize*/
  0,                            /*tp_itemsize*/
  0,                            /*tp_dealloc*/
  0,                            /*tp_print*/
  0,                            /*tp_getattr*/
  0,                            /*tp_setattr*/
  0,                            /*tp_compare*/
  (reprfunc) wg_record_repr,    /*tp_repr*/
  0,                            /*tp_as_number*/
  0,                            /*tp_as_sequence*/
  0,                            /*tp_as_mapping*/
  0,                            /*tp_hash */
  0,                            /*tp_call*/
  (reprfunc) wg_record_repr,    /*tp_str*/
  0,                            /*tp_getattro*/
  0,                            /*tp_setattro*/
  0,                            /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT,           /*tp_flags*/
  "WGandalf db record object",  /* tp_doc */
};

/** Method table */
static PyMethodDef wgdb_methods[] = {
  {"attach_database",  (PyCFunction) wgdb_attach_database,
   METH_VARARGS | METH_KEYWORDS,
   "Connect to a shared memory database. If the database with the "\
   "given name does not exist, it is created."},
  {"delete_database",  wgdb_delete_database, METH_VARARGS,
   "Delete a shared memory database."},
  {"create_record",  wgdb_create_record, METH_VARARGS,
   "Create a record with given length."},
  {"get_first_record",  wgdb_get_first_record, METH_VARARGS,
   "Fetch first record from database."},
  {"get_next_record",  wgdb_get_next_record, METH_VARARGS,
   "Fetch next record from database."},
  {"get_record_len",  wgdb_get_record_len, METH_VARARGS,
   "Get record length (number of fields)."},
  {"set_null_field",  wgdb_set_null_field, METH_VARARGS,
   "Set NULL (empty) field value."},
  {"set_int_field",  wgdb_set_int_field, METH_VARARGS,
   "Set integer field value."},
  {"set_double_field",  wgdb_set_double_field, METH_VARARGS,
   "Set double precision field value."},
  {"set_str_field",  wgdb_set_str_field, METH_VARARGS,
   "Set string field value."},
  {"get_decoded_field",  wgdb_get_decoded_field, METH_VARARGS,
   "Get field data decoded to corresponding Python type."},
  {NULL, NULL, 0, NULL} /* terminator */
};

/* ============== Functions ============== */

/* Wrapped wgdb API
 * uses wg_database_type object to store the database pointer
 * when making calls from python. This type is not available
 * generally (using non-restricted values for the pointer
 * would cause segfaults), only by calling wgdb_attach_database().
 */

/* Functions for attaching and deleting */

/** Attach to memory database.
 *  Python wrapper to wg_attach_database()
 */

static PyObject * wgdb_attach_database(PyObject *self, PyObject *args,
                                        PyObject *kwds) {
  wg_database *db;
  char *shmname = NULL;
  wg_int sz = 0;
  static char *kwlist[] = {"shmname", "size", NULL};

  if(!PyArg_ParseTupleAndKeywords(args, kwds, "|si", kwlist, &shmname, &sz))
    return NULL;
  
  db = (wg_database *) wg_database_type.tp_alloc(&wg_database_type, 0);
  if(!db) return NULL;

  /* Now try to actually connect. Note that this may create
   * a new database if none is found with a matching name.
   */
  db->db = (void *) wg_attach_database(shmname, sz);
  if(!db->db) {
    PyErr_SetString(wgdb_error, "Failed to attach to database.");
    /* XXX: should we free it here or does the garbage collector
     * pick it up anyway? (no references)
     */
    wg_database_type.tp_free(db);
    return NULL;
  }
  Py_INCREF(db);
  return (PyObject *) db;
}

/** Delete memory database.
 *  Python wrapper to wg_delete_database()
 */

static PyObject * wgdb_delete_database(PyObject *self, PyObject *args) {
  char *shmname = NULL;
  int err = 0;

  if(!PyArg_ParseTuple(args, "|s", &shmname))
    return NULL;

  err = wg_delete_database(shmname);
  if(err) {
    PyErr_SetString(wgdb_error, "Failed to delete the database.");
    return NULL;
  }

  Py_INCREF(Py_None);
  return Py_None;
}

/* Functions to manipulate records. The record is also
 * represented as a custom type to avoid dealing with word
 * size issues on different platforms. So the type is essentially
 * a container for the record pointer.
 */

/** Create a record with given length.
 *  Python wrapper to wg_create_record()
 */

static PyObject * wgdb_create_record(PyObject *self, PyObject *args) {
  PyObject *db = NULL;
  wg_int length = 0;
  wg_record *rec;

  if(!PyArg_ParseTuple(args, "Oi", &db, &length))
    return NULL;

  /* Validate the database object */
  VALIDATE_DB(db)

  /* Build a new record object */
  rec = (wg_record *) wg_record_type.tp_alloc(&wg_record_type, 0);
  if(!rec) return NULL;

  rec->rec = wg_create_record(((wg_database *) db)->db, length);
  if(!rec->rec) {
    PyErr_SetString(wgdb_error, "Failed to create a record.");
    wg_record_type.tp_free(rec);
    return NULL;
  }

  Py_INCREF(rec);
  return (PyObject *) rec;
}

/** Fetch first record from database.
 *  Python wrapper to wg_get_first_record()
 */

static PyObject * wgdb_get_first_record(PyObject *self, PyObject *args) {
  PyObject *db = NULL;
  wg_record *rec;

  if(!PyArg_ParseTuple(args, "O", &db))
    return NULL;

  /* Validate the database object */
  VALIDATE_DB(db)

  /* Build a new record object */
  rec = (wg_record *) wg_record_type.tp_alloc(&wg_record_type, 0);
  if(!rec) return NULL;

  rec->rec = wg_get_first_record(((wg_database *) db)->db);
  if(!rec->rec) {
    PyErr_SetString(wgdb_error, "Failed to fetch a record.");
    wg_record_type.tp_free(rec);
    return NULL;
  }

  Py_INCREF(rec);
  return (PyObject *) rec;
}

/** Fetch next record from database.
 *  Python wrapper to wg_get_next_record()
 */

static PyObject * wgdb_get_next_record(PyObject *self, PyObject *args) {
  PyObject *db = NULL, *prev = NULL;
  wg_record *rec;

  if(!PyArg_ParseTuple(args, "OO", &db, &prev))
    return NULL;

  /* Validate the arguments */
  VALIDATE_DB(db)
  VALIDATE_REC(prev)

  /* Build a new record object */
  rec = (wg_record *) wg_record_type.tp_alloc(&wg_record_type, 0);
  if(!rec) return NULL;

  rec->rec = wg_get_next_record(((wg_database *) db)->db,
    ((wg_record *) prev)->rec);
  if(!rec->rec) {
    PyErr_SetString(wgdb_error, "Failed to fetch a record.");
    wg_record_type.tp_free(rec);
    return NULL;
  }

  Py_INCREF(rec);
  return (PyObject *) rec;
}

/** Get record length (number of fields).
 *  Python wrapper to wg_get_record_len()
 */

static PyObject * wgdb_get_record_len(PyObject *self, PyObject *args) {
  PyObject *db = NULL, *rec = NULL;
  wg_int len = 0;

  if(!PyArg_ParseTuple(args, "OO", &db, &rec))
    return NULL;

  /* Validate the arguments */
  VALIDATE_DB(db)
  VALIDATE_REC(rec)

  len = wg_get_record_len(((wg_database *) db)->db,
    ((wg_record *) rec)->rec);
  if(len < 0) {
    PyErr_SetString(wgdb_error, "Failed to get the record length.");
    return NULL;
  }
  return Py_BuildValue("i", (int) len);
}


/* Functions to manipulate field contents.
 *
 * We use higher level (encoding is done by wgdb lib) functions to
 * write data. Since similar API is not present for reading records,
 * decoding is done by this module. We provide only one function for
 * reading data: the concept behind that is that the side which holds
 * the data, also knows it's type. So when writing, we know the type
 * in Python, and so decide the function to use _on Python side_ of
 * the fence. When reading, the type is known to the wgdb module, so
 * we decide on the function internally and return the data _in the
 * format known to Python_.
 */

/** Set NULL field.
 *  Pseudo wrapper. Exposing wg_set_field() directly is not useful
 *  other than for writing NULL-s. This function provides that
 *  functionality by explicitly setting the written data to 0.
 */

static PyObject *wgdb_set_null_field(PyObject *self, PyObject *args) {
  PyObject *db = NULL, *rec = NULL;
  wg_int fieldnr, err = 0;

  if(!PyArg_ParseTuple(args, "OOi", &db, &rec, &fieldnr))
    return NULL;

  /* Validate the arguments */
  VALIDATE_DB(db)
  VALIDATE_REC(rec)

  err = wg_set_field(((wg_database *) db)->db,
    ((wg_record *) rec)->rec, fieldnr, 0);
  if(err < 0) {
    PyErr_SetString(wgdb_error, "Failed to set NULL field.");
    return NULL;
  }

  Py_INCREF(Py_None);
  return Py_None;
}

/** Set integer fields.
 *  Python wrapper to wg_set_int_field()
 */

static PyObject *wgdb_set_int_field(PyObject *self, PyObject *args) {
  PyObject *db = NULL, *rec = NULL;
  wg_int fieldnr, data, err = 0;

  if(!PyArg_ParseTuple(args, "OOii", &db, &rec, &fieldnr, &data))
    return NULL;

  /* Validate the arguments */
  VALIDATE_DB(db)
  VALIDATE_REC(rec)

  err = wg_set_int_field(((wg_database *) db)->db,
    ((wg_record *) rec)->rec, fieldnr, data);
  if(err < 0) {
    PyErr_SetString(wgdb_error, "Failed to set integer field.");
    return NULL;
  }

  Py_INCREF(Py_None);
  return Py_None;
}

/** Set double fields.
 *  Python wrapper to wg_set_double_field()
 */

static PyObject *wgdb_set_double_field(PyObject *self, PyObject *args) {
  PyObject *db = NULL, *rec = NULL;
  wg_int fieldnr, err = 0;
  double data;

  if(!PyArg_ParseTuple(args, "OOid", &db, &rec, &fieldnr, &data))
    return NULL;

  /* Validate the arguments */
  VALIDATE_DB(db)
  VALIDATE_REC(rec)

  err = wg_set_double_field(((wg_database *) db)->db,
    ((wg_record *) rec)->rec, fieldnr, data);
  if(err < 0) {
    PyErr_SetString(wgdb_error, "Failed to set double field.");
    return NULL;
  }

  Py_INCREF(Py_None);
  return Py_None;
}

/** Set string fields. Embedded \0 bytes are not allowed (i.e. \0 is
 *  treated as a standard string terminator).
 *  Python wrapper to wg_set_str_field()
 *  XXX: handle language as well?
 */

static PyObject *wgdb_set_str_field(PyObject *self, PyObject *args) {
  PyObject *db = NULL, *rec = NULL;
  wg_int fieldnr, err = 0;
  char *data;

  if(!PyArg_ParseTuple(args, "OOis", &db, &rec, &fieldnr, &data))
    return NULL;

  /* Validate the arguments */
  VALIDATE_DB(db)
  VALIDATE_REC(rec)

  err = wg_set_str_field(((wg_database *) db)->db,
    ((wg_record *) rec)->rec, fieldnr, data);
  if(err < 0) {
    PyErr_SetString(wgdb_error, "Failed to set string field.");
    return NULL;
  }

  Py_INCREF(Py_None);
  return Py_None;
}

/** Get decoded field value.
 *  XXX: Currently only supports NULL, int, double and str.
 */

static PyObject *wgdb_get_decoded_field(PyObject *self, PyObject *args) {
  PyObject *db = NULL, *rec = NULL;
  wg_int fieldnr, fdata, ftype;
  
  if(!PyArg_ParseTuple(args, "OOi", &db, &rec, &fieldnr))
    return NULL;

  /* Validate the arguments */
  VALIDATE_DB(db)
  VALIDATE_REC(rec)

  ftype = wg_get_field_type(((wg_database *) db)->db,
    ((wg_record *) rec)->rec, fieldnr);
  if(!ftype) {
    PyErr_SetString(wgdb_error, "Failed to get field type.");
    return NULL;
  }

  /* A little hack. If it's NULL type, there is no point in reading
   * the value. Also, with the current API, wg_get_field() will return
   * 0 which is indistinguishable from error.
   */
  if(ftype==WG_NULLTYPE) {
    Py_INCREF(Py_None);
    return Py_None;
  }

  fdata = wg_get_field(((wg_database *) db)->db,
    ((wg_record *) rec)->rec, fieldnr);
  if(fdata==WG_ILLEGAL) {
    PyErr_SetString(wgdb_error, "Failed to get field data.");
    return NULL;
  }

  if(ftype==WG_INTTYPE) {
    wg_int ddata = wg_decode_int(((wg_database *) db)->db, fdata);
    return Py_BuildValue("i", ddata);
  }
  else if(ftype==WG_DOUBLETYPE) {
    double ddata = wg_decode_double(((wg_database *) db)->db, fdata);
    return Py_BuildValue("d", ddata);
  }
  else if(ftype==WG_STRTYPE) {
    char *ddata = wg_decode_str(((wg_database *) db)->db, fdata);
    /* Data is copied here, no leaking */
    return Py_BuildValue("s", ddata);
  }
  else {
    char buf[80];
    snprintf(buf, 80, "Cannot handle field type %d.", ftype);
    PyErr_SetString(wgdb_error, buf);
    return NULL;
  }
}

/* additional functions that could be implemented/wrapped here:

wg_int wg_set_rec_field(void* db, void* record, wg_int fieldnr, void* data);
wg_int wg_get_field_type(void* db, void* record, wg_int fieldnr);
?? char* wg_decode_str_lang(void* db, wg_int data);
wg_int wg_decode_str_len(void* db, wg_int data); 
?? wg_int wg_decode_str_lang_len(void* db, wg_int data); 
wg_int wg_decode_str_copy(void* db, wg_int data, char* strbuf, wg_int buflen);
?? wg_int wg_decode_str_lang_copy(void* db, wg_int data, char* langbuf, wg_int buflen);

*/

/* Methods for the wg_database_type objects
 * XXX: might need _dealloc() that calls wg_detach_database()
 */

/** String representation of database object. This is used for both
 * repr() and str()
 */
static PyObject *wg_database_repr(wg_database * obj) {
  /* XXX: this is incompatible with eval(). If a need to
   * eval() such representations should arise, new initialization
   * function is also needed for the type.
   */
  return PyString_FromFormat("<WGandalf database at %x>",
    (unsigned int) obj->db);
}

/** String representation of record object. This is used for both
 * repr() and str()
 */
static PyObject *wg_record_repr(wg_record * obj) {
  /* XXX: incompatible with eval(). */
  return PyString_FromFormat("<WGandalf db record at %x>",
    (unsigned int) obj->rec);
}

/** Initialize module.
 *  Standard entry point for Python extension modules, executed
 *  during import.
 */

PyMODINIT_FUNC initwgdb(void) {
  PyObject *m;
  
  wg_database_type.tp_new = PyType_GenericNew;
  if (PyType_Ready(&wg_database_type) < 0)
    return;
  
  wg_record_type.tp_new = PyType_GenericNew;
  if (PyType_Ready(&wg_record_type) < 0)
    return;
  
  m = Py_InitModule3("wgdb", wgdb_methods, "wgandalf database adapter");
  if(!m) return;

  wgdb_error = PyErr_NewException("wgdb.error", NULL, NULL);
  Py_INCREF(wgdb_error);
  PyModule_AddObject(m, "error", wgdb_error);  
}
