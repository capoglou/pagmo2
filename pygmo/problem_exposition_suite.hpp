/* Copyright 2017 PaGMO development team

This file is part of the PaGMO library.

The PaGMO library is free software; you can redistribute it and/or modify
it under the terms of either:

  * the GNU Lesser General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your
    option) any later version.

or

  * the GNU General Public License as published by the Free Software
    Foundation; either version 3 of the License, or (at your option) any
    later version.

or both in parallel, as here.

The PaGMO library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received copies of the GNU General Public License and the
GNU Lesser General Public License along with the PaGMO library.  If not,
see https://www.gnu.org/licenses/. */

#ifndef PYGMO_PROBLEM_EXPOSITION_SUITE_HPP
#define PYGMO_PROBLEM_EXPOSITION_SUITE_HPP

#include "python_includes.hpp"

#include <boost/python/args.hpp>
#include <boost/python/class.hpp>
#include <boost/python/default_call_policies.hpp>
#include <boost/python/init.hpp>
#include <boost/python/make_constructor.hpp>
#include <boost/python/object.hpp>
#include <boost/python/scope.hpp>
#include <cassert>
#include <string>
#include <utility>

#include <pagmo/problem.hpp>
#include <pagmo/problems/translate.hpp>

#include "common_utils.hpp"
#include "pygmo_classes.hpp"

namespace pygmo
{

namespace bp = boost::python;

// Wrapper for the best known method.
template <typename Prob>
inline bp::object best_known_wrapper(const Prob &p)
{
    return v_to_a(p.best_known());
}

// Constructor of translate from problem and translation vector.
// NOTE: it seems like returning a raw pointer is fine. See the examples here:
// http://www.boost.org/doc/libs/1_61_0/libs/python/test/injected.cpp
template <typename Prob>
inline pagmo::translate *translate_init(const Prob &p, const bp::object &o)
{
    return ::new pagmo::translate(p, to_vd(o));
}

// Make Python init from ctor above.
template <typename Prob>
inline auto make_translate_init()
    -> decltype(bp::make_constructor(&translate_init<Prob>, bp::default_call_policies(),
                                     (bp::arg(std::declval<const char *>()), bp::arg(std::declval<const char *>()))))
{
    return bp::make_constructor(&translate_init<Prob>, bp::default_call_policies(),
                                (bp::arg("prob"), bp::arg("translation")));
}

// Constructor of decompose from problem and weight, z, method and bool flag.
template <typename Prob>
inline pagmo::decompose *decompose_init(const Prob &p, const bp::object &weight, const bp::object &z,
                                        const std::string &method, bool adapt_ideal)
{
    return ::new pagmo::decompose(p, to_vd(weight), to_vd(z), method, adapt_ideal);
}

// Make a python init from the above ctor.
template <typename Prob>
inline auto make_decompose_init()
    -> decltype(bp::make_constructor(&decompose_init<Prob>, bp::default_call_policies(),
                                     (bp::arg(std::declval<const char *>()), bp::arg(std::declval<const char *>()),
                                      bp::arg(std::declval<const char *>()),
                                      bp::arg(std::declval<const char *>()) = std::string(std::declval<const char *>()),
                                      bp::arg(std::declval<const char *>()) = false)))
{
    return bp::make_constructor(&decompose_init<Prob>, bp::default_call_policies(),
                                (bp::arg("prob"), bp::arg("weight"), bp::arg("z"),
                                 bp::arg("method") = std::string("weighted"), bp::arg("adapt_ideal") = false));
}

// Expose a problem ctor from a user-defined problem.
template <typename Prob>
inline void problem_prob_init()
{
    assert(problem_ptr.get() != nullptr);
    auto &prob_class = *problem_ptr;
    prob_class.def(bp::init<const Prob &>((bp::arg("p"))));
}

// Main problem exposition function.
template <typename Prob>
inline bp::class_<Prob> expose_problem(const char *name, const char *descr)
{
    assert(problem_ptr.get() != nullptr);
    assert(translate_ptr.get() != nullptr);
    assert(decompose_ptr.get() != nullptr);
    auto &problem_class = *problem_ptr;
    auto &tp_class = *translate_ptr;
    auto &dp_class = *decompose_ptr;
    // We require all problems to be def-ctible at the bare minimum.
    bp::class_<Prob> c(name, descr, bp::init<>());
    // Mark it as a C++ problem.
    c.attr("_pygmo_cpp_problem") = true;

    // Expose the problem constructor from Prob.
    problem_prob_init<Prob>();
    // Expose extract.
    problem_class.def("_cpp_extract", &generic_cpp_extract<pagmo::problem, Prob>);

    // Expose translate's constructor from Prob and translation vector.
    tp_class
        .def("__init__", make_translate_init<Prob>())
        // Extract.
        .def("_cpp_extract", &generic_cpp_extract<pagmo::translate, Prob>);

    // Expose decompose's constructor from Prob.
    dp_class
        .def("__init__", make_decompose_init<Prob>())
        // Extract.
        .def("_cpp_extract", &generic_cpp_extract<pagmo::decompose, Prob>);

    // Add the problem to the problems submodule.
    bp::scope().attr("problems").attr(name) = c;

    return c;
}
}

#endif
