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

#ifndef PAGMO_ALGORITHMS_SADE_HPP
#define PAGMO_ALGORITHMS_SADE_HPP

#include <iomanip>
#include <numeric> //std::iota
#include <random>
#include <string>
#include <tuple>
#include <utility> //std::swap

#include "../algorithm.hpp"
#include "../exceptions.hpp"
#include "../io.hpp"
#include "../population.hpp"
#include "../rng.hpp"
#include "../utils/generic.hpp"

namespace pagmo
{
/// Self-adaptive Differential Evolution Algorithm
/**
 * \image html adaptation.jpg "Adapt already!".
 *
 * Two different variants of the Differential Evolution algorithm exploiting the idea of self-adaptation.
 *
 * The original Differential Evolution algorithm (pagmo::de) can be significantly improved introducing the
 * idea of parameter self-adaptation. Many different proposals have been made to self-adapt both the CR and the F
 * parameters
 * of the original differential evolution algorithm. In PaGMO we implement two different mechanisms we found effective.
 * The first one, proposed by Brest et al., does not make use of the DE operators to produce new
 * values for F and CR and, strictly speaking, is thus not self-adaptation, rather parameter control.
 * The resulting DE variant is often referred to as jDE. The second variant
 * here implemented is inspired by the ideas introduced by Elsayed et al. and uses a variaton of the selected DE
 * operator to produce new
 * CR anf F parameters for each individual. We refer to this variant as to iDE.
 *
 * @note There exist an algorithm referred to as SaDE in the literature. This is not the algorithm implemented in PaGMO.
 *
 * @note The feasibility correction, that is the correction applied to an allele when some mutation puts it outside
 * the allowed box-bounds, is here done by creating a random number in the bounds.
 *
 * @see (jDE) - Brest, J., Greiner, S., Bošković, B., Mernik, M., & Zumer, V. (2006). Self-adapting control parameters
 * in differential evolution: a comparative study on numerical benchmark problems. Evolutionary Computation, IEEE
 * Transactions on, 10(6), 646-657. Chicago
 * @see (iDE) - Elsayed, S. M., Sarker, R. A., & Essam, D. L. (2011, June). Differential evolution with multiple
 * strategies for solving CEC2011 real-world numerical optimization problems. In Evolutionary Computation (CEC), 2011
 * IEEE Congress on (pp. 1041-1048). IEEE.
 */
class sade
{
public:
    /// Single entry of the log (gen, fevals, best, F, CR, dx, df)
    typedef std::tuple<unsigned int, unsigned long long, double, double, double, double, double> log_line_type;
    /// The log
    typedef std::vector<log_line_type> log_type;

    /// Constructor.
    /**
     * Constructs self-adaptive differential evolution
     *
     * Two self-adaptation variants are available to control the F and CR parameters:
     *
     * @code
     * 1 - jDE (Brest et al.)                       2 - iDE (Elsayed at al.)
     * @endcode
     *
     * The following variants are available to produce a mutant vector:
     *
     * @code
     * 1 - best/1/exp                               2. - rand/1/exp
     * 3 - rand-to-best/1/exp                       4. - best/2/exp
     * 5 - rand/2/exp                               6. - best/1/bin
     * 7 - rand/1/bin                               8. - rand-to-best/1/bin
     * 9 - best/2/bin                               10. - rand/2/bin
     * 11. - rand/3/exp                             12. - rand/3/bin
     * 13. - best/3/exp                             14. - best/3/bin
     * 15. - rand-to-current/2/exp                  16. - rand-to-current/2/bin
     * 17. - rand-to-best-and-current/2/exp         18. - rand-to-best-and-current/2/bin
     * @endcode
     *
     * The first ten are the classical mutation variants introduced in the orginal DE algorithm, the remaining ones are,
     * instead, considered in the work by Elsayed et al.
     *
     * @param[in] gen number of generations.
     * @param[in] variant mutation variant (dafault variant is 2: /rand/1/exp)
     * @param[in] variant_adptv F and CR parameter adaptation scheme to be used (one of 1..2)
     * @param[in] ftol stopping criteria on the x tolerance (default is 1e-6)
     * @param[in] xtol stopping criteria on the f tolerance (default is 1e-6)
     * @param[in] memory when true the adapted parameters CR anf F are not reset between successive calls to the evolve
     method
     * @param[in] seed seed used by the internal random number generator (default is random)

     * @throws std::invalid_argument if \p variant_adptv is not one of 0,1
     * @throws std::invalid_argument if variant is not one of 1, .., 18
     */
    sade(unsigned int gen = 1u, unsigned int variant = 2u, unsigned int variant_adptv = 1u, double ftol = 1e-6,
         double xtol = 1e-6, bool memory = false, unsigned int seed = pagmo::random_device::next())
        : m_gen(gen), m_F(), m_CR(), m_variant(variant), m_variant_adptv(variant_adptv), m_Ftol(ftol), m_xtol(xtol),
          m_memory(memory), m_e(seed), m_seed(seed), m_verbosity(0u), m_log()
    {
        if (variant < 1u || variant > 18u) {
            pagmo_throw(std::invalid_argument,
                        "The Differential Evolution mutation variant must be in [1, .., 18], while a value of "
                            + std::to_string(variant) + " was detected.");
        }
        if (variant_adptv < 1u || variant_adptv > 2u) {
            pagmo_throw(std::invalid_argument, "The variant for self-adaptation mus be in [1,2], while a value of "
                                                   + std::to_string(variant_adptv) + " was detected.");
        }
    }

    /// Algorithm evolve method (juice implementation of the algorithm)
    /**
     *
     * Evolves the population for a maximum number of generations, until one of
     * tolerances set on the population flatness (x_tol, f_tol) are met.
     *
     * @param[in] pop population to be evolved
     * @return evolved population
     * @throws std::invalid_argument if the problem is multi-objective or constrained or stochastic
     * @throws std::invalid_argument if the population size is not at least 7
     */
    population evolve(population pop) const
    {
        // We store some useful variables
        const auto &prob = pop.get_problem(); // This is a const reference, so using set_seed for example will not be
                                              // allowed (pop.set_problem_seed is)
        auto dim = prob.get_nx();             // This getter does not return a const reference but a copy
        const auto bounds = prob.get_bounds();
        const auto &lb = bounds.first;
        const auto &ub = bounds.second;
        auto NP = pop.size();
        auto prob_f_dimension = prob.get_nf();
        auto fevals0 = prob.get_fevals(); // disount for the already made fevals
        unsigned int count = 1u;          // regulates the screen output

        // PREAMBLE-------------------------------------------------------------------------------------------------
        // We start by checking that the problem is suitable for this
        // particular algorithm.
        if (prob.get_nc() != 0u) {
            pagmo_throw(std::invalid_argument, "Non linear constraints detected in " + prob.get_name() + " instance. "
                                                   + get_name() + " cannot deal with them");
        }
        if (prob_f_dimension != 1u) {
            pagmo_throw(std::invalid_argument, "Multiple objectives detected in " + prob.get_name() + " instance. "
                                                   + get_name() + " cannot deal with them");
        }
        if (prob.is_stochastic()) {
            pagmo_throw(std::invalid_argument,
                        "The problem appears to be stochastic " + get_name() + " cannot deal with it");
        }
        // Get out if there is nothing to do.
        if (m_gen == 0u) {
            return pop;
        }
        if (pop.size() < 7u) {
            pagmo_throw(std::invalid_argument, prob.get_name() + " needs at least 7 individuals in the population, "
                                                   + std::to_string(pop.size()) + " detected");
        }
        // ---------------------------------------------------------------------------------------------------------

        // No throws, all valid: we clear the logs
        m_log.clear();

        // Some vectors used during evolution are declared.
        vector_double tmp(dim);                              // contains the mutated candidate
        std::uniform_real_distribution<double> drng(0., 1.); // to generate a number in [0, 1)
        std::normal_distribution<double> n_dist(0., 1.);     // to generate a normally distributed number
        std::uniform_int_distribution<vector_double::size_type> c_idx(
            0u, dim - 1u); // to generate a random index in the chromosome
        std::uniform_int_distribution<vector_double::size_type> p_idx(0u, NP - 1u); // to generate a random index in pop

        // We extract from pop the chromosomes and fitness associated
        auto popold = pop.get_x();
        auto fit = pop.get_f();
        auto popnew = popold;

        // Initialise the global bests
        auto best_idx = pop.best_idx();
        vector_double::size_type worst_idx = 0u;
        auto gbX = popnew[best_idx];
        auto gbfit = fit[best_idx];
        // the best decision vector of a generation
        auto gbIter = gbX;
        std::vector<vector_double::size_type> r(7); // indexes of 7 selected population members

        // Initialize the F and CR vectors
        if ((m_CR.size() != NP) || (m_F.size() != NP) || (!m_memory)) {
            m_CR.resize(NP);
            m_F.resize(NP);
            if (m_variant_adptv == 1u) {
                for (decltype(NP) i = 0u; i < NP; ++i) {
                    m_CR[i] = drng(m_e);
                    m_F[i] = drng(m_e) * 0.9 + 0.1;
                }
            } else if (m_variant_adptv == 2u) {
                for (decltype(NP) i = 0u; i < NP; ++i) {
                    m_CR[i] = n_dist(m_e) * 0.15 + 0.5;
                    m_F[i] = n_dist(m_e) * 0.15 + 0.5;
                }
            }
        }
        // Initialize the global and iteration bests for F and CR
        double gbF = m_F[0];   // initialization to the 0 ind, will soon be forgotten
        double gbCR = m_CR[0]; // initialization to the 0 ind, will soon be forgotten
        double gbIterF = gbF;
        double gbIterCR = gbCR;
        // We initialize the global best for F and CR as the first individual (this will soon be forgotten)

        // Main DE iterations
        for (decltype(m_gen) gen = 1u; gen <= m_gen; ++gen) {
            // Start of the loop through the population
            for (decltype(NP) i = 0u; i < NP; ++i) {
                /*-----We select at random 5 indexes from the population---------------------------------*/
                std::vector<vector_double::size_type> idxs(NP);
                std::iota(idxs.begin(), idxs.end(), vector_double::size_type(0u));
                for (auto j = 0u; j < 7u; ++j) { // Durstenfeld's algorithm to select 7 indexes at random
                    auto idx = std::uniform_int_distribution<vector_double::size_type>(0u, NP - 1u - j)(m_e);
                    r[j] = idxs[idx];
                    std::swap(idxs[idx], idxs[NP - 1u - j]);
                }

                // Adapt amplification factor and crossover probability for jDE
                double F = 0., CR = 0.;
                if (m_variant_adptv == 1u) {
                    F = (drng(m_e) < 0.9) ? m_F[i] : drng(m_e) * 0.9 + 0.1;
                    CR = (drng(m_e) < 0.9) ? m_CR[i] : drng(m_e);
                }

                /*-------DE/best/1/exp--------------------------------------------------------------------*/
                /*-------The oldest DE variant but still not bad. However, we have found several----------*/
                /*-------optimization problems where misconvergence occurs.-------------------------------*/
                if (m_variant == 1u) {
                    if (m_variant_adptv == 2u) {
                        F = gbIterF + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[r[2]]);
                        CR = gbIterCR + n_dist(m_e) * 0.5 * (m_CR[r[1]] - m_CR[r[2]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    auto L = 0u;
                    do {
                        tmp[n] = gbIter[n] + F * (popold[r[1]][n] - popold[r[2]][n]);
                        n = (n + 1u) % dim;
                        ++L;
                    } while ((drng(m_e) < CR) && (L < dim));
                }

                /*-------DE/rand/1/exp-------------------------------------------------------------------*/
                else if (m_variant == 2u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[r[0]] + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[r[2]]);
                        CR = m_CR[r[0]] + n_dist(m_e) * 0.5 * (m_CR[r[1]] - m_CR[r[2]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    decltype(dim) L = 0u;
                    do {
                        tmp[n] = popold[r[0]][n] + F * (popold[r[1]][n] - popold[r[2]][n]);
                        n = (n + 1u) % dim;
                        ++L;
                    } while ((drng(m_e) < CR) && (L < dim));
                }
                /*-------DE/rand-to-best/1/exp-----------------------------------------------------------*/
                else if (m_variant == 3u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[i] + n_dist(m_e) * 0.5 * (gbIterF - m_F[i])
                            + n_dist(m_e) * 0.5 * (m_F[r[0]] - m_F[r[1]]);
                        CR = m_CR[i] + n_dist(m_e) * 0.5 * (gbIterCR - m_CR[i])
                             + n_dist(m_e) * 0.5 * (m_CR[r[0]] - m_CR[r[1]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    auto L = 0u;
                    do {
                        tmp[n] = tmp[n] + F * (gbIter[n] - tmp[n]) + F * (popold[r[0]][n] - popold[r[1]][n]);
                        n = (n + 1u) % dim;
                        ++L;
                    } while ((drng(m_e) < CR) && (L < dim));
                }
                /*-------DE/best/2/exp is another powerful variant worth trying--------------------------*/
                else if (m_variant == 4u) {
                    if (m_variant_adptv == 2u) {
                        F = gbIterF + n_dist(m_e) * 0.5 * (m_F[r[0]] - m_F[r[1]])
                            + n_dist(m_e) * 0.5 * (m_F[r[2]] - m_F[r[3]]);
                        CR = gbIterCR + n_dist(m_e) * 0.5 * (m_CR[r[0]] - m_CR[r[1]])
                             + n_dist(m_e) * 0.5 * (m_CR[r[2]] - m_CR[r[3]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    auto L = 0u;
                    do {
                        tmp[n] = gbIter[n] + (popold[r[0]][n] - popold[r[1]][n]) * F
                                 + (popold[r[2]][n] - popold[r[3]][n]) * F;
                        n = (n + 1u) % dim;
                        ++L;
                    } while ((drng(m_e) < CR) && (L < dim));
                }
                /*-------DE/rand/2/exp seems to be a robust optimizer for many functions-------------------*/
                else if (m_variant == 5u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[r[4]] + n_dist(m_e) * 0.5 * (m_F[r[0]] - m_F[r[1]])
                            + n_dist(m_e) * 0.5 * (m_F[r[2]] - m_F[r[3]]);
                        CR = m_CR[r[4]] + n_dist(m_e) * 0.5 * (m_CR[r[0]] - m_CR[r[1]])
                             + n_dist(m_e) * 0.5 * (m_CR[r[2]] - m_CR[r[3]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    auto L = 0u;
                    do {
                        tmp[n] = popold[r[4]][n] + (popold[r[0]][n] - popold[r[1]][n]) * F
                                 + (popold[r[2]][n] - popold[r[3]][n]) * F;
                        n = (n + 1u) % dim;
                        ++L;
                    } while ((drng(m_e) < CR) && (L < dim));
                }

                /*=======Essentially same strategies but BINOMIAL CROSSOVER===============================*/
                /*-------DE/best/1/bin--------------------------------------------------------------------*/
                else if (m_variant == 6u) {
                    if (m_variant_adptv == 2u) {
                        F = gbIterF + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[r[2]]);
                        CR = gbIterCR + n_dist(m_e) * 0.5 * (m_CR[r[1]] - m_CR[r[2]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    for (decltype(dim) L = 0u; L < dim; ++L) {   /* perform Dc binomial trials */
                        if ((drng(m_e) < CR) || L + 1u == dim) { /* change at least one parameter */
                            tmp[n] = gbIter[n] + F * (popold[r[1]][n] - popold[r[2]][n]);
                        }
                        n = (n + 1u) % dim;
                    }
                }
                /*-------DE/rand/1/bin-------------------------------------------------------------------*/
                else if (m_variant == 7u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[r[0]] + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[r[2]]);
                        CR = m_CR[r[0]] + n_dist(m_e) * 0.5 * (m_CR[r[1]] - m_CR[r[2]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    for (decltype(dim) L = 0u; L < dim; ++L) {   /* perform Dc binomial trials */
                        if ((drng(m_e) < CR) || L + 1u == dim) { /* change at least one parameter */
                            tmp[n] = popold[r[0]][n] + F * (popold[r[1]][n] - popold[r[2]][n]);
                        }
                        n = (n + 1u) % dim;
                    }
                }
                /*-------DE/rand-to-best/1/bin-----------------------------------------------------------*/
                else if (m_variant == 8u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[i] + n_dist(m_e) * 0.5 * (gbIterF - m_F[i])
                            + n_dist(m_e) * 0.5 * (m_F[r[0]] - m_F[r[1]]);
                        CR = m_CR[i] + n_dist(m_e) * 0.5 * (gbIterCR - m_CR[i])
                             + n_dist(m_e) * 0.5 * (m_CR[r[0]] - m_CR[r[1]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    for (decltype(dim) L = 0u; L < dim; ++L) {   /* perform Dc binomial trials */
                        if ((drng(m_e) < CR) || L + 1u == dim) { /* change at least one parameter */
                            tmp[n] = tmp[n] + F * (gbIter[n] - tmp[n]) + F * (popold[r[0]][n] - popold[r[1]][n]);
                        }
                        n = (n + 1u) % dim;
                    }
                }
                /*-------DE/best/2/bin--------------------------------------------------------------------*/
                else if (m_variant == 9u) {
                    if (m_variant_adptv == 2u) {
                        F = gbIterF + n_dist(m_e) * 0.5 * (m_F[r[0]] - m_F[r[1]])
                            + n_dist(m_e) * 0.5 * (m_F[r[2]] - m_F[r[3]]);
                        CR = gbIterCR + n_dist(m_e) * 0.5 * (m_CR[r[0]] - m_CR[r[1]])
                             + n_dist(m_e) * 0.5 * (m_CR[r[2]] - m_CR[r[3]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    for (decltype(dim) L = 0u; L < dim; ++L) {   /* perform Dc binomial trials */
                        if ((drng(m_e) < CR) || L + 1u == dim) { /* change at least one parameter */
                            tmp[n] = gbIter[n] + (popold[r[0]][n] - popold[r[1]][n]) * F
                                     + (popold[r[2]][n] - popold[r[3]][n]) * F;
                        }
                        n = (n + 1u) % dim;
                    }
                }
                /*-------DE/rand/2/bin--------------------------------------------------------------------*/
                else if (m_variant == 10u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[r[4]] + n_dist(m_e) * 0.5 * (m_F[r[0]] - m_F[r[1]])
                            + n_dist(m_e) * 0.5 * (m_F[r[2]] - m_F[r[3]]);
                        CR = m_CR[r[4]] + n_dist(m_e) * 0.5 * (m_CR[r[0]] - m_CR[r[1]])
                             + n_dist(m_e) * 0.5 * (m_CR[r[2]] - m_CR[r[3]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    for (decltype(dim) L = 0u; L < dim; ++L) {   /* perform Dc binomial trials */
                        if ((drng(m_e) < CR) || L + 1u == dim) { /* change at least one parameter */
                            tmp[n] = popold[r[4]][n] + (popold[r[0]][n] - popold[r[1]][n]) * F
                                     + (popold[r[2]][n] - popold[r[3]][n]) * F;
                        }
                        n = (n + 1u) % dim;
                    }
                }
                /*-------DE/rand/3/exp --------------------------------------------------------------------*/
                else if (m_variant == 11u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[r[0]] + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[r[2]])
                            + n_dist(m_e) * 0.5 * (m_F[r[3]] - m_F[r[4]]) + n_dist(m_e) * 0.5 * (m_F[r[5]] - m_F[r[6]]);
                        CR = m_CR[r[4]] + n_dist(m_e) * 0.5 * (m_CR[r[0]] + m_CR[r[1]] - m_CR[r[2]] - m_CR[r[3]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    auto L = 0u;
                    do {
                        tmp[n] = popold[r[0]][n] + (popold[r[1]][n] - popold[r[2]][n]) * F
                                 + (popold[r[3]][n] - popold[r[4]][n]) * F + (popold[r[5]][n] - popold[r[6]][n]) * F;
                        n = (n + 1u) % dim;
                        ++L;
                    } while ((drng(m_e) < CR) && (L < dim));
                }
                /*-------DE/rand/3/bin --------------------------------------------------------------------*/
                else if (m_variant == 12u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[r[0]] + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[r[2]])
                            + n_dist(m_e) * 0.5 * (m_F[r[3]] - m_F[r[4]]) + n_dist(m_e) * 0.5 * (m_F[r[5]] - m_F[r[6]]);
                        CR = m_CR[r[4]] + n_dist(m_e) * 0.5 * (m_CR[r[0]] + m_CR[r[1]] - m_CR[r[2]] - m_CR[r[3]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    for (decltype(dim) L = 0u; L < dim; ++L) {   /* perform Dc binomial trials */
                        if ((drng(m_e) < CR) || L + 1u == dim) { /* change at least one parameter */
                            tmp[n] = popold[r[0]][n] + (popold[r[1]][n] - popold[r[2]][n]) * F
                                     + (popold[r[3]][n] - popold[r[4]][n]) * F
                                     + (popold[r[5]][n] - popold[r[6]][n]) * F;
                        }
                        n = (n + 1u) % dim;
                    }
                }
                /*-------DE/best/3/exp --------------------------------------------------------------------*/
                else if (m_variant == 13u) {
                    if (m_variant_adptv == 2u) {
                        F = gbIterF + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[r[2]])
                            + n_dist(m_e) * 0.5 * (m_F[r[3]] - m_F[r[4]]) + n_dist(m_e) * 0.5 * (m_F[r[5]] - m_F[r[6]]);
                        CR = gbIterCR + n_dist(m_e) * 0.5 * (m_CR[r[0]] + m_CR[r[1]] - m_CR[r[2]] - m_CR[r[3]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    auto L = 0u;
                    do {
                        tmp[n] = gbIter[n] + (popold[r[1]][n] - popold[r[2]][n]) * F
                                 + (popold[r[3]][n] - popold[r[4]][n]) * F + (popold[r[5]][n] - popold[r[6]][n]) * F;
                        n = (n + 1u) % dim;
                        ++L;
                    } while ((drng(m_e) < CR) && (L < dim));
                }
                /*-------DE/best/3/bin --------------------------------------------------------------------*/
                else if (m_variant == 14u) {
                    if (m_variant_adptv == 2u) {
                        F = gbIterF + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[r[2]])
                            + n_dist(m_e) * 0.5 * (m_F[r[3]] - m_F[r[4]]) + n_dist(m_e) * 0.5 * (m_F[r[5]] - m_F[r[6]]);
                        CR = gbIterCR + n_dist(m_e) * 0.5 * (m_CR[r[0]] + m_CR[r[1]] - m_CR[r[2]] - m_CR[r[3]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    for (decltype(dim) L = 0u; L < dim; ++L) {   /* perform Dc binomial trials */
                        if ((drng(m_e) < CR) || L + 1u == dim) { /* change at least one parameter */
                            tmp[n] = gbIter[n] + (popold[r[1]][n] - popold[r[2]][n]) * F
                                     + (popold[r[3]][n] - popold[r[4]][n]) * F
                                     + (popold[r[5]][n] - popold[r[6]][n]) * F;
                        }
                        n = (n + 1u) % dim;
                    }
                }
                /*-------DE/rand-to-current/2/exp --------------------------------------------------------------------*/
                else if (m_variant == 15u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[r[0]] + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[i])
                            + n_dist(m_e) * 0.5 * (m_F[r[3]] - m_F[r[4]]);
                        CR = m_CR[r[0]] + n_dist(m_e) * 0.5 * (m_CR[r[1]] - m_CR[i])
                             + n_dist(m_e) * 0.5 * (m_CR[r[3]] - m_CR[r[4]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    auto L = 0u;
                    do {
                        tmp[n] = popold[r[0]][n] + (popold[r[1]][n] - popold[i][n]) * F
                                 + (popold[r[2]][n] - popold[r[3]][n]) * F;
                        n = (n + 1u) % dim;
                        ++L;
                    } while ((drng(m_e) < CR) && (L < dim));
                }
                /*-------DE/rand-to-current/2/bin --------------------------------------------------------------------*/
                else if (m_variant == 16u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[r[0]] + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[i])
                            + n_dist(m_e) * 0.5 * (m_F[r[3]] - m_F[r[4]]);
                        CR = m_CR[r[0]] + n_dist(m_e) * 0.5 * (m_CR[r[1]] - m_CR[i])
                             + n_dist(m_e) * 0.5 * (m_CR[r[3]] - m_CR[r[4]]);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    for (decltype(dim) L = 0u; L < dim; ++L) {   /* perform Dc binomial trials */
                        if ((drng(m_e) < CR) || L + 1u == dim) { /* change at least one parameter */
                            tmp[n] = popold[r[0]][n] + (popold[r[1]][n] - popold[i][n]) * F
                                     + (popold[r[2]][n] - popold[r[3]][n]) * F;
                        }
                        n = (n + 1u) % dim;
                    }
                }
                /*-------DE/rand-to-best-and-current/2/exp
                   --------------------------------------------------------------------*/
                else if (m_variant == 17u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[r[0]] + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[i])
                            - n_dist(m_e) * 0.5 * (m_F[r[2]] - gbIterF);
                        CR = m_CR[r[0]] + n_dist(m_e) * 0.5 * (m_CR[r[1]] - m_CR[i])
                             - n_dist(m_e) * 0.5 * (m_CR[r[3]] - gbIterCR);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    auto L = 0u;
                    do {
                        tmp[n] = popold[r[0]][n] + (popold[r[1]][n] - popold[i][n]) * F
                                 - (popold[r[2]][n] - gbIter[n]) * F;
                        n = (n + 1u) % dim;
                        ++L;
                    } while ((drng(m_e) < CR) && (L < dim));
                }
                /*-------DE/rand-to-best-and-current/2/bin
                   --------------------------------------------------------------------*/
                else if (m_variant == 18u) {
                    if (m_variant_adptv == 2u) {
                        F = m_F[r[0]] + n_dist(m_e) * 0.5 * (m_F[r[1]] - m_F[i])
                            - n_dist(m_e) * 0.5 * (m_F[r[2]] - gbIterF);
                        CR = m_CR[r[0]] + n_dist(m_e) * 0.5 * (m_CR[r[1]] - m_CR[i])
                             - n_dist(m_e) * 0.5 * (m_CR[r[3]] - gbIterCR);
                    }
                    tmp = popold[i];
                    auto n = c_idx(m_e);
                    for (decltype(dim) L = 0u; L < dim; ++L) {   /* perform Dc binomial trials */
                        if ((drng(m_e) < CR) || L + 1u == dim) { /* change at least one parameter */
                            tmp[n] = popold[r[0]][n] + (popold[r[1]][n] - popold[i][n]) * F
                                     - (popold[r[2]][n] - gbIter[n]) * F;
                        }
                        n = (n + 1u) % dim;
                    }
                }

                /*==Trial mutation now in tmp. force feasibility and see how good this choice really was.==*/
                // a) feasibility
                for (decltype(dim) j = 0u; j < dim; ++j) {
                    if ((tmp[j] < lb[j]) || (tmp[j] > ub[j])) {
                        tmp[j] = uniform_real_from_range(lb[j], ub[j], m_e);
                    }
                }
                // b) how good?
                auto newfitness = prob.fitness(tmp); /* Evaluates tmp[] */
                if (newfitness[0] <= fit[i][0]) {    /* improved objective function value ? */
                    fit[i] = newfitness;
                    popnew[i] = tmp;
                    // updates the individual in pop (avoiding to recompute the objective function)
                    pop.set_xf(i, popnew[i], newfitness);
                    // Update the adapted parameters
                    m_CR[i] = CR;
                    m_F[i] = F;

                    if (newfitness[0] <= gbfit[0]) {
                        /* if so...*/
                        gbfit = newfitness; /* reset gbfit to new low...*/
                        gbX = popnew[i];
                        gbF = F;   /* these were forgotten in PaGMOlegacy */
                        gbCR = CR; /* these were forgotten in PaGMOlegacy */
                    }
                } else {
                    popnew[i] = popold[i];
                }
            } // End of one generation
            /* Save best population member of current iteration */
            gbIter = gbX;
            gbIterF = gbF;
            gbIterCR = gbCR;
            /* swap population arrays. New generation becomes old one */
            std::swap(popold, popnew);

            // Check the exit conditions (every 40 generations)
            double dx = 0., df = 0.;
            if (gen % 40u == 0u) {
                best_idx = pop.best_idx();
                worst_idx = pop.worst_idx();
                for (decltype(dim) i = 0u; i < dim; ++i) {
                    dx += std::abs(pop.get_x()[worst_idx][i] - pop.get_x()[best_idx][i]);
                }
                if (dx < m_xtol) {
                    if (m_verbosity > 0u) {
                        std::cout << "Exit condition -- xtol < " << m_xtol << std::endl;
                    }
                    return pop;
                }

                df = std::abs(pop.get_f()[worst_idx][0] - pop.get_f()[best_idx][0]);
                if (df < m_Ftol) {
                    if (m_verbosity > 0u) {
                        std::cout << "Exit condition -- ftol < " << m_Ftol << std::endl;
                    }
                    return pop;
                }
            }

            // Logs and prints (verbosity modes > 1: a line is added every m_verbosity generations)
            if (m_verbosity > 0u) {
                // Every m_verbosity generations print a log line
                if (gen % m_verbosity == 1u || m_verbosity == 1u) {
                    best_idx = pop.best_idx();
                    worst_idx = pop.worst_idx();
                    dx = 0.;
                    // The population flattness in chromosome
                    for (decltype(dim) i = 0u; i < dim; ++i) {
                        dx += std::abs(pop.get_x()[worst_idx][i] - pop.get_x()[best_idx][i]);
                    }
                    // The population flattness in fitness
                    df = std::abs(pop.get_f()[worst_idx][0] - pop.get_f()[best_idx][0]);
                    // Every 50 lines print the column names
                    if (count % 50u == 1u) {
                        print("\n", std::setw(7), "Gen:", std::setw(15), "Fevals:", std::setw(15), "Best:",
                              std::setw(15), "F:", std::setw(15), "CR:", std::setw(15), "dx:", std::setw(15),
                              std::setw(15), "df:", '\n');
                    }
                    print(std::setw(7), gen, std::setw(15), prob.get_fevals() - fevals0, std::setw(15),
                          pop.get_f()[best_idx][0], std::setw(15), gbIterF, std::setw(15), gbIterCR, std::setw(15), dx,
                          std::setw(15), df, '\n');
                    ++count;
                    // Logs
                    m_log.push_back(log_line_type(gen, prob.get_fevals() - fevals0, pop.get_f()[best_idx][0], gbIterF,
                                                  gbIterCR, dx, df));
                }
            }
        } // end main DE iterations
        if (m_verbosity) {
            std::cout << "Exit condition -- generations = " << m_gen << std::endl;
        }
        return pop;
    }

    /// Sets the algorithm seed
    void set_seed(unsigned int seed)
    {
        m_seed = seed;
    };
    /// Gets the seed
    unsigned int get_seed() const
    {
        return m_seed;
    }
    /// Sets the algorithm verbosity
    /**
     * Sets the verbosity level of the screen output and of the
     * log returned by get_log(). \p level can be:
     * - 0: no verbosity
     * - >0: will print and log one line each \p level generations.
     *
     * Example (verbosity 1):
     * @code
     * Gen:        Fevals:          Best:             F:            CR:            dx:            df:
     *  301           4515       0.668472       0.374983       0.502932    0.000276682    0.000388866
     *  302           4530       0.668472       0.374983       0.502932    0.000213271     0.00020986
     *  303           4545       0.668426       0.598243       0.234825    0.000167061    0.000186339
     *  304           4560       0.668426       0.598243       0.234825    0.000217549    0.000144896
     *  305           4575       0.668339       0.807236       0.863048    0.000192539    0.000232005
     *  306           4590       0.668339       0.807236       0.863048    0.000143711    0.000229041
     *  307           4605       0.668307       0.374983       0.820731    0.000163919    0.000245393

     * @endcode
     * Gen, is the generation number, Fevals the number of function evaluation used, Best is the best fitness
     * function currently in the population, F is the F used to create the best so far, CR
     * the CR used to create the best so far, dx is the population flatness evaluated as the distance between
     * the decisions vector of the best and of the worst individual and df is the population flatness evaluated
     * as the distance between the fitness of the best and of the worst individual.
     *
     * @param level verbosity level
     */
    void set_verbosity(unsigned int level)
    {
        m_verbosity = level;
    };
    /// Gets the verbosity level
    unsigned int get_verbosity() const
    {
        return m_verbosity;
    }
    /// Get generations
    unsigned int get_gen() const
    {
        return m_gen;
    }
    /// Algorithm name
    std::string get_name() const
    {
        return "Self-adaptive Differential Evolution";
    }
    /// Extra informations
    std::string get_extra_info() const
    {
        return "\tGenerations: " + std::to_string(m_gen) + "\n\tVariant: " + std::to_string(m_variant)
               + "\n\tSelf adaptation variant: " + std::to_string(m_variant_adptv) + "\n\tStopping xtol: "
               + std::to_string(m_xtol) + "\n\tStopping ftol: " + std::to_string(m_Ftol) + "\n\tMemory: "
               + std::to_string(m_memory) + "\n\tVerbosity: " + std::to_string(m_verbosity) + "\n\tSeed: "
               + std::to_string(m_seed);
    }
    /// Get log
    /**
     * A log containing relevant quantities monitoring the last call to evolve. Each element of the returned
     * <tt> std::vector </tt> is a sade::log_line_type containing: Gen, Fevals, Best, F, CR, dx, df as described
     * in sade::set_verbosity
     * @return an <tt> std::vector </tt> of sade::log_line_type containing the logged values Gen, Fevals, Best, F, CR,
     * dx, df
     */
    const log_type &get_log() const
    {
        return m_log;
    }
    /// Serialization
    template <typename Archive>
    void serialize(Archive &ar)
    {
        ar(m_gen, m_F, m_CR, m_variant, m_variant_adptv, m_Ftol, m_xtol, m_memory, m_e, m_seed, m_verbosity, m_log);
    }

private:
    unsigned int m_gen;
    mutable vector_double m_F;
    mutable vector_double m_CR;
    unsigned int m_variant;
    unsigned int m_variant_adptv;
    double m_Ftol;
    double m_xtol;
    bool m_memory;
    mutable detail::random_engine_type m_e;
    unsigned int m_seed;
    unsigned int m_verbosity;
    mutable log_type m_log;
};

} // namespace pagmo

PAGMO_REGISTER_ALGORITHM(pagmo::sade)

#endif
