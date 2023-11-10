/*
 * Copyright 2023 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/***********************************************************************************/
/* This file is automatically generated using bindtool and can be manually edited  */
/* The following lines can be configured to regenerate this file during cmake      */
/* If manual edits are made, the following tags should be modified accordingly.    */
/* BINDTOOL_GEN_AUTOMATIC(0)                                                       */
/* BINDTOOL_USE_PYGCCXML(0)                                                        */
/* BINDTOOL_HEADER_FILE(xfecframe_demapper_cb.h)                                   */
/* BINDTOOL_HEADER_FILE_HASH(4edcac58ee1541b4e089b2aab16f01c6)                     */
/***********************************************************************************/

#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

#include <gnuradio/dvbs2rx/xfecframe_demapper_cb.h>
// pydoc.h is automatically generated in the build directory
#include <xfecframe_demapper_cb_pydoc.h>

void bind_xfecframe_demapper_cb(py::module& m)
{
    using xfecframe_demapper_cb = gr::dvbs2rx::xfecframe_demapper_cb;

    py::class_<xfecframe_demapper_cb,
               gr::block,
               gr::basic_block,
               std::shared_ptr<xfecframe_demapper_cb>>(
        m, "xfecframe_demapper_cb", D(xfecframe_demapper_cb))

        .def(py::init(&xfecframe_demapper_cb::make),
             py::arg("framesize"),
             py::arg("rate"),
             py::arg("constellation"),
             D(xfecframe_demapper_cb, make))

        .def(
            "get_snr", &xfecframe_demapper_cb::get_snr, D(xfecframe_demapper_cb, get_snr))

        ;
}
