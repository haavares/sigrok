/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sigrokdecode.h" /* First, so we avoid a _POSIX_C_SOURCE warning. */
#include "sigrokdecode-internal.h"
#include "config.h"

/**
 * Get the value of a Python object's attribute, returned as a newly
 * allocated char *.
 *
 * @param py_obj The object to probe.
 * @param attr Name of the attribute to retrieve.
 * @param outstr ptr to char * storage to be filled in.
 *
 * @return SRD_OK upon success, a (negative) error code otherwise.
 *         The 'outstr' argument points to a malloc()ed string upon success.
 */
SRD_PRIV int py_attr_as_str(const PyObject *py_obj, const char *attr,
			    char **outstr)
{
	PyObject *py_str;
	int ret;

	if (!PyObject_HasAttrString((PyObject *)py_obj, attr)) {
		srd_dbg("%s object has no attribute '%s'.",
			Py_TYPE(py_obj)->tp_name, attr);
		return SRD_ERR_PYTHON;
	}

	if (!(py_str = PyObject_GetAttrString((PyObject *)py_obj, attr))) {
		srd_exception_catch("");
		return SRD_ERR_PYTHON;
	}

	if (!PyUnicode_Check(py_str)) {
		srd_dbg("%s attribute should be a string, but is a %s.",
			attr, Py_TYPE(py_str)->tp_name);
		Py_DecRef(py_str);
		return SRD_ERR_PYTHON;
	}

	ret = py_str_as_str(py_str, outstr);
	Py_DecRef(py_str);

	return ret;
}

/**
 * Get the value of a Python dictionary item, returned as a newly
 * allocated char *.
 *
 * @param py_obj The dictionary to probe.
 * @param attr Key of the item to retrieve.
 * @param outstr ptr to char * storage to be filled in.
 *
 * @return SRD_OK upon success, a (negative) error code otherwise.
 *         The 'outstr' argument points to a malloc()ed string upon success.
 */
SRD_PRIV int py_dictitem_as_str(const PyObject *py_obj, const char *key,
				char **outstr)
{
	PyObject *py_value;
	int ret;

	if (!PyDict_Check((PyObject *)py_obj)) {
		srd_dbg("Object is a %s, not a dictionary.",
			Py_TYPE((PyObject *)py_obj)->tp_name);
		return SRD_ERR_PYTHON;
	}

	if (!(py_value = PyDict_GetItemString((PyObject *)py_obj, key))) {
		srd_dbg("Dictionary has no attribute '%s'.", key);
		return SRD_ERR_PYTHON;
	}

	if (!PyUnicode_Check(py_value)) {
		srd_dbg("Dictionary value for %s should be a string, but is "
			"a %s.", key, Py_TYPE(py_value)->tp_name);
		return SRD_ERR_PYTHON;
	}

	ret = py_str_as_str(py_value, outstr);

	return ret;
}

/**
 * Get the value of a Python unicode string object, returned as a newly
 * allocated char *.
 *
 * @param py_str The unicode string object.
 * @param outstr ptr to char * storage to be filled in.
 *
 * @return SRD_OK upon success, a (negative) error code otherwise.
 *         The 'outstr' argument points to a malloc()ed string upon success.
 */
SRD_PRIV int py_str_as_str(const PyObject *py_str, char **outstr)
{
	PyObject *py_encstr;
	int ret;
	char *str;

	py_encstr = NULL;
	str = NULL;
	ret = SRD_OK;

	if (!PyUnicode_Check((PyObject *)py_str)) {
		srd_dbg("Object is a %s, not a string object.",
			Py_TYPE((PyObject *)py_str)->tp_name);
		ret = SRD_ERR_PYTHON;
		goto err_out;
	}

	if (!(py_encstr = PyUnicode_AsEncodedString((PyObject *)py_str,
	    "utf-8", NULL))) {
		ret = SRD_ERR_PYTHON;
		goto err_out;
	}
	if (!(str = PyBytes_AS_STRING(py_encstr))) {
		ret = SRD_ERR_PYTHON;
		goto err_out;
	}

	if (!(*outstr = g_strdup(str))) {
		srd_dbg("Failed to g_malloc() outstr.");
		ret = SRD_ERR_MALLOC;
		goto err_out;
	}

err_out:
	if (py_encstr)
		Py_XDECREF(py_encstr);

	if (PyErr_Occurred()) {
		srd_exception_catch("string conversion failed");
	}

	return ret;
}

/**
 * Convert a Python list of unicode strings to a NULL-terminated UTF8-encoded
 * char * array. The caller must g_free() each string when finished.
 *
 * @param py_strlist The list object.
 * @param outstr ptr to char ** storage to be filled in.
 *
 * @return SRD_OK upon success, a (negative) error code otherwise.
 *         The 'outstr' argument points to a g_malloc()ed char** upon success.
 */
SRD_PRIV int py_strlist_to_char(const PyObject *py_strlist, char ***outstr)
{
	PyObject *py_str;
	int list_len, i;
	char **out, *str;

	list_len = PyList_Size((PyObject *)py_strlist);
	if (!(out = g_try_malloc(sizeof(char *) * (list_len + 1)))) {
		srd_err("Failed to g_malloc() 'out'.");
		return SRD_ERR_MALLOC;
	}
	for (i = 0; i < list_len; i++) {
		if (!(py_str = PyUnicode_AsEncodedString(
		    PyList_GetItem((PyObject *)py_strlist, i), "utf-8", NULL)))
			return SRD_ERR_PYTHON;
		if (!(str = PyBytes_AS_STRING(py_str)))
			return SRD_ERR_PYTHON;
		out[i] = g_strdup(str);
	}
	out[i] = NULL;
	*outstr = out;

	return SRD_OK;
}
