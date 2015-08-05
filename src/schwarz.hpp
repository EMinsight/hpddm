/*
   This file is part of HPDDM.

   Author(s): Pierre Jolivet <pierre.jolivet@inf.ethz.ch>
              Frédéric Nataf <nataf@ann.jussieu.fr>
        Date: 2013-03-10

   Copyright (C) 2011-2014 Université de Grenoble
                 2015      Eidgenössische Technische Hochschule Zürich

   HPDDM is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published
   by the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   HPDDM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with HPDDM.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SCHWARZ_
#define _SCHWARZ_

#include <set>

namespace HPDDM {
/* Class: Schwarz
 *
 *  A class for solving problems using Schwarz methods that inherits from <Preconditioner>.
 *
 * Template Parameters:
 *    Solver         - Solver used for the factorization of local matrices.
 *    CoarseOperator - Class of the coarse operator.
 *    S              - 'S'ymmetric or 'G'eneral coarse operator.
 *    K              - Scalar type. */
template<template<class> class Solver, template<class> class CoarseSolver, char S, class K>
class Schwarz : public Preconditioner<Solver, CoarseOperator<CoarseSolver, S, K>, K> {
    public:
        /* Enum: Prcndtnr
         *
         *  Defines the Schwarz method used as a preconditioner.
         *
         * NO           - No preconditioner.
         * SY           - Symmetric preconditioner, e.g. Additive Schwarz method.
         * GE           - Nonsymmetric preconditioner, e.g. Restricted Additive Schwarz method.
         * OS           - Optimized symmetric preconditioner, e.g. Optimized Schwarz method.
         * OG           - Optimized nonsymmetric preconditioner, e.g. Optimized Restricted Additive Schwarz method. */
        enum class Prcndtnr : char {
            NO, SY, GE, OS, OG, AD, BA
        };
    private:
        /* Variable: d
         *  Local partition of unity. */
        const typename Wrapper<K>::ul_type* _d;
        /* Variable: type
         *  Type of <Prcndtnr> used in <Schwarz::apply> and <Schwarz::deflation>. */
        Prcndtnr                         _type;
    public:
        Schwarz() : _d() { }
        ~Schwarz() { _d = nullptr; }
        /* Typedef: super
         *  Type of the immediate parent class <Preconditioner>. */
        typedef Preconditioner<Solver, CoarseOperator<CoarseSolver, S, K>, K> super;
        /* Function: initialize
         *  Sets <Schwarz::d>. */
        template<class Container = std::vector<int>>
        void initialize(typename Wrapper<K>::ul_type* const& d) {
            _d = d;
        }
        /* Function: callNumfact
         *  Factorizes <Subdomain::a> or another user-supplied matrix, useful for <Prcndtnr::OS> and <Prcndtnr::OG>. */
        void callNumfact(MatrixCSR<K>* const& A = nullptr) {
            Option& opt = *Option::get();
            if(A != nullptr) {
                if(opt["schwarz_method"] == 1)
                    _type = Prcndtnr::OS;
                else
                    _type = Prcndtnr::OG;
            }
            else {
                if(opt["schwarz_method"] == 3)
                    _type = Prcndtnr::SY;
                else if(opt["schwarz_method"] == 5)
                    _type = Prcndtnr::NO;
                else {
                    _type = Prcndtnr::GE;
                    opt["schwarz_method"] = 0;
                }
            }
            super::_s.numfact(_type == Prcndtnr::OS || _type == Prcndtnr::OG ? A : Subdomain<K>::_a, _type == Prcndtnr::OS ? true : false);
        }
        void setMatrix(MatrixCSR<K>* const& a) {
            bool fact = super::setMatrix(a);
            if(fact) {
                using type = alias<Solver<K>>;
                super::_s.~type();
                super::_s.numfact(a);
            }
        }
        /* Function: multiplicityScaling
         *
         *  Builds the multiplicity scaling.
         *
         * Parameter:
         *    d              - Array of values. */
        void multiplicityScaling(typename Wrapper<K>::ul_type* const d) const {
            for(unsigned short i = 0; i < Subdomain<K>::_map.size(); ++i) {
                typename Wrapper<K>::ul_type* const recv = reinterpret_cast<typename Wrapper<K>::ul_type*>(Subdomain<K>::_rbuff[i]);
                typename Wrapper<K>::ul_type* const send = reinterpret_cast<typename Wrapper<K>::ul_type*>(Subdomain<K>::_sbuff[i]);
                MPI_Irecv(recv, Subdomain<K>::_map[i].second.size(), Wrapper<typename Wrapper<K>::ul_type>::mpi_type(), Subdomain<K>::_map[i].first, 0, Subdomain<K>::_communicator, Subdomain<K>::_rq + i);
                Wrapper<typename Wrapper<K>::ul_type>::gthr(Subdomain<K>::_map[i].second.size(), d, send, Subdomain<K>::_map[i].second.data());
                MPI_Isend(send, Subdomain<K>::_map[i].second.size(), Wrapper<typename Wrapper<K>::ul_type>::mpi_type(), Subdomain<K>::_map[i].first, 0, Subdomain<K>::_communicator, Subdomain<K>::_rq + Subdomain<K>::_map.size() + i);
            }
            std::fill(d, d + Subdomain<K>::_dof, 1.0);
            for(unsigned short i = 0; i < Subdomain<K>::_map.size(); ++i) {
                int index;
                MPI_Waitany(Subdomain<K>::_map.size(), Subdomain<K>::_rq, &index, MPI_STATUS_IGNORE);
                typename Wrapper<K>::ul_type* const recv = reinterpret_cast<typename Wrapper<K>::ul_type*>(Subdomain<K>::_rbuff[index]);
                typename Wrapper<K>::ul_type* const send = reinterpret_cast<typename Wrapper<K>::ul_type*>(Subdomain<K>::_sbuff[index]);
                for(unsigned int j = 0; j < Subdomain<K>::_map[index].second.size(); ++j) {
                    if(std::abs(send[j]) < HPDDM_EPS)
                        d[Subdomain<K>::_map[index].second[j]] = 0.0;
                    else
                        d[Subdomain<K>::_map[index].second[j]] /= 1.0 + d[Subdomain<K>::_map[index].second[j]] * recv[j] / send[j];
                }
            }
            MPI_Waitall(Subdomain<K>::_map.size(), Subdomain<K>::_rq + Subdomain<K>::_map.size(), MPI_STATUSES_IGNORE);
        }
        /* Function: getScaling
         *  Returns a constant pointer to <Schwarz::d>. */
        const typename Wrapper<K>::ul_type* getScaling() const { return _d; }
        /* Function: deflation
         *
         *  Computes a coarse correction.
         *
         * Template parameter:
         *    excluded       - True if the master processes are excluded from the domain decomposition, false otherwise. 
         *
         * Parameters:
         *    in             - Input vector.
         *    out            - Output vector.
         *    fuse           - Number of fused reductions (optional). */
        template<bool excluded>
        void deflation(const K* const in, K* const out, const unsigned short& fuse = 0) const {
            if(fuse > 0) {
                super::_co->reallocateRHS(const_cast<K*&>(super::_uc), fuse);
                std::copy_n(out + Subdomain<K>::_dof, fuse, super::_uc + super::getLocal());
            }
            if(excluded)
                super::_co->template callSolver<excluded>(super::_uc, fuse);
            else {
                Wrapper<K>::diag(Subdomain<K>::_dof, _d, in, out);                                                                                                                                                  // out = D in
                Wrapper<K>::gemv(&(Wrapper<K>::transc), &(Subdomain<K>::_dof), super::getAddrLocal(), &(Wrapper<K>::d__1), *super::_ev, &(Subdomain<K>::_dof), out, &i__1, &(Wrapper<K>::d__0), super::_uc, &i__1); // _uc = _ev^T D in
                super::_co->template callSolver<excluded>(super::_uc, fuse);                                                                                                                                        // _uc = E \ _ev^T D in
                Wrapper<K>::gemv(&transa, &(Subdomain<K>::_dof), super::getAddrLocal(), &(Wrapper<K>::d__1), *super::_ev, &(Subdomain<K>::_dof), super::_uc, &i__1, &(Wrapper<K>::d__0), out, &i__1);               // out = _ev E \ _ev^T D in
                if(_type != Prcndtnr::AD) {
                    Wrapper<K>::diag(Subdomain<K>::_dof, _d, out);
                    Subdomain<K>::exchange(out);
                }
            }
            if(fuse > 0)
                std::copy_n(super::_uc + super::getLocal(), fuse, out + Subdomain<K>::_dof);
        }
#if HPDDM_ICOLLECTIVE
        /* Function: Ideflation
         *
         *  Computes the first part of a coarse correction asynchronously.
         *
         * Template parameter:
         *    excluded       - True if the master processes are excluded from the domain decomposition, false otherwise. 
         *
         * Parameters:
         *    in             - Input vector.
         *    out            - Output vector.
         *    rq             - MPI request to check completion of the MPI transfers.
         *    fuse           - Number of fused reductions (optional). */
        template<bool excluded>
        void Ideflation(const K* const in, K* const out, MPI_Request* rq, const unsigned short& fuse = 0) const {
            if(fuse > 0) {
                super::_co->reallocateRHS(const_cast<K*&>(super::_uc), fuse);
                std::copy_n(out + Subdomain<K>::_dof, fuse, super::_uc + super::getLocal());
            }
            if(excluded)
                super::_co->template IcallSolver<excluded>(super::_uc, rq, fuse);
            else {
                Wrapper<K>::diag(Subdomain<K>::_dof, _d, in, out);
                Wrapper<K>::gemv(&(Wrapper<K>::transc), &(Subdomain<K>::_dof), super::getAddrLocal(), &(Wrapper<K>::d__1), *super::_ev, &(Subdomain<K>::_dof), out, &i__1, &(Wrapper<K>::d__0), super::_uc, &i__1);
                super::_co->template IcallSolver<excluded>(super::_uc, rq, fuse);
            }
            if(fuse > 0)
                std::copy_n(super::_uc + super::getLocal(), fuse, out + Subdomain<K>::_dof);
        }
#endif // HPDDM_ICOLLECTIVE
        template<bool excluded>
        void deflation(K* const out, const unsigned short& fuse = 0) const {
            deflation<excluded>(nullptr, out, fuse);
        }
        /* Function: buildTwo
         *
         *  Assembles and factorizes the coarse operator by calling <Preconditioner::buildTwo>.
         *
         * Template Parameter:
         *    excluded       - Greater than 0 if the master processes are excluded from the domain decomposition, equal to 0 otherwise.
         *
         * Parameter:
         *    comm           - Global MPI communicator.
         *
         * See also: <Bdd::buildTwo>, <Feti::buildTwo>. */
        template<unsigned short excluded = 0>
        std::pair<MPI_Request, const K*>* buildTwo(const MPI_Comm& comm) {
            return super::template buildTwo<excluded, 2>(std::move(MatrixMultiplication<Schwarz<Solver, CoarseSolver, S, K>, K>(*this)), comm);
        }
        /* Function: apply
         *
         *  Applies the global Schwarz preconditioner.
         *
         * Template Parameter:
         *    excluded       - Greater than 0 if the master processes are excluded from the domain decomposition, equal to 0 otherwise.
         *
         * Parameters:
         *    in             - Input vector, modified internally !
         *    out            - Output vector.
         *    fuse           - Number of fused reductions (optional). */
        template<bool excluded = false>
        void apply(const K* const in, K* const out, const unsigned short& mu = 1, K* work = nullptr, const unsigned short& fuse = 0) const {
            int correction = std::max(Option::get()->val("schwarz_coarse_correction"), -1.0);
            if(!super::_co || correction == -1) {
                if(_type == Prcndtnr::NO)
                    std::copy_n(in, mu * Subdomain<K>::_dof, out);
                else if(_type == Prcndtnr::GE || _type == Prcndtnr::OG) {
                    if(!excluded) {
                        super::_s.solve(in, out, mu);
                        Wrapper<K>::diag(Subdomain<K>::_dof, mu, _d, out);
                        Subdomain<K>::exchange(out, mu);                                                     // out = D A \ in
                    }
                }
                else {
                    if(!excluded) {
                        if(_type == Prcndtnr::OS) {
                            Wrapper<K>::diag(Subdomain<K>::_dof, mu, _d, in, out);
                            super::_s.solve(out, mu);
                            Wrapper<K>::diag(Subdomain<K>::_dof, mu, _d, out);
                        }
                        else
                            super::_s.solve(in, out, mu);
                        Subdomain<K>::exchange(out, mu);                                                     // out = A \ in
                    }
                }
            }
            else {
                if(!work)
                    work = const_cast<K*>(in);
                else
                    std::copy_n(in, Subdomain<K>::_dof, work);
                if(correction == 1) {
#if HPDDM_ICOLLECTIVE
                    MPI_Request rq[2];
                    Ideflation<excluded>(in, out, rq, fuse);
                    if(!excluded) {
                        super::_s.solve(work);                                                                                                                                                                // out = A \ in
                        MPI_Waitall(2, rq, MPI_STATUSES_IGNORE);
                        Wrapper<K>::gemv(&transa, &(Subdomain<K>::_dof), super::getAddrLocal(), &(Wrapper<K>::d__1), *super::_ev, &(Subdomain<K>::_dof), super::_uc, &i__1, &(Wrapper<K>::d__0), out, &i__1); // out = Z E \ Z^T in
                        Wrapper<K>::axpy(&(Subdomain<K>::_dof), &(Wrapper<K>::d__1), work, &i__1, out, &i__1);
                        Wrapper<K>::diag(Subdomain<K>::_dof, _d, out);
                        Subdomain<K>::exchange(out);                                                                                                                                                          // out = Z E \ Z^T in + A \ in
                    }
                    else
                        MPI_Wait(rq + 1, MPI_STATUS_IGNORE);
#else
                    deflation<excluded>(in, out, fuse);
                    if(!excluded) {
                        super::_s.solve(work);
                        Wrapper<K>::axpy(&(Subdomain<K>::_dof), &(Wrapper<K>::d__1), work, &i__1, out, &i__1);
                        Wrapper<K>::diag(Subdomain<K>::_dof, _d, out);
                        Subdomain<K>::exchange(out);
                    }
#endif // HPDDM_ICOLLECTIVE
                }
                else {
                    deflation<excluded>(in, out, fuse);                                                        // out = Z E \ Z^T in
                    if(!excluded) {
                        Wrapper<K>::template csrmv<'C'>(&transa, &(Subdomain<K>::_dof), &(Subdomain<K>::_dof), &(Wrapper<K>::d__2), Subdomain<K>::_a->_sym, Subdomain<K>::_a->_a, Subdomain<K>::_a->_ia, Subdomain<K>::_a->_ja, out, &(Wrapper<K>::d__1), work);
                        Wrapper<K>::diag(Subdomain<K>::_dof, _d, work);
                        Subdomain<K>::exchange(work);                                                          //  in = (I - A Z E \ Z^T) in
                        if(_type == Prcndtnr::OS)
                            Wrapper<K>::diag(Subdomain<K>::_dof, _d, work);
                        super::_s.solve(work);
                        Wrapper<K>::diag(Subdomain<K>::_dof, _d, work);
                        Subdomain<K>::exchange(work);                                                          //  in = D A \ (I - A Z E \ Z^T) in
                        if(correction == 2) {
                            K* tmp = new K[Subdomain<K>::_dof];
                            GMV(work, tmp, mu);
                            deflation<excluded>(nullptr, tmp, fuse);
                            Wrapper<K>::axpy(&(Subdomain<K>::_dof), &(Wrapper<K>::d__2), tmp, &i__1, work, &i__1);
                            delete [] tmp;
                        }
                        Wrapper<K>::axpy(&(Subdomain<K>::_dof), &(Wrapper<K>::d__1), work, &i__1, out, &i__1); // out = D A \ (I - A Z E \ Z^T) in + Z E \ Z^T in
                    }
                }
            }
        }
        /* Function: scaleIntoOverlap
         *
         *  Scales the input matrix using <Schwarz::d> on the overlap and sets the output matrix to zero elsewhere.
         *
         * Parameters:
         *    A              - Input matrix.
         *    B              - Output matrix used in GenEO.
         *
         * See also: <Schwarz::solveGEVP>. */
        void scaleIntoOverlap(const MatrixCSR<K>* const& A, MatrixCSR<K>*& B) const {
            std::set<unsigned int> intoOverlap;
            for(const pairNeighbor& neighbor : Subdomain<K>::_map)
                for(unsigned int i : neighbor.second)
                    if(_d[i] > HPDDM_EPS)
                        intoOverlap.insert(i);
            std::vector<std::vector<std::pair<unsigned int, K>>> tmp(intoOverlap.size());
            unsigned int k, iPrev = 0;
#pragma omp parallel for schedule(static, HPDDM_GRANULARITY) reduction(+ : iPrev)
            for(k = 0; k < intoOverlap.size(); ++k) {
                auto it = std::next(intoOverlap.cbegin(), k);
                tmp[k].reserve(A->_ia[*it + 1] - A->_ia[*it]);
                for(unsigned int j = A->_ia[*it]; j < A->_ia[*it + 1]; ++j) {
                    K value = _d[*it] * _d[A->_ja[j]] * A->_a[j];
                    if(std::abs(value) > HPDDM_EPS && intoOverlap.find(A->_ja[j]) != intoOverlap.cend())
                        tmp[k].emplace_back(A->_ja[j], value);
                }
                iPrev += tmp[k].size();
            }
            int nnz = iPrev;
            if(B != nullptr)
                delete B;
            B = new MatrixCSR<K>(Subdomain<K>::_dof, Subdomain<K>::_dof, nnz, A->_sym);
            nnz = iPrev = k = 0;
            for(unsigned int i : intoOverlap) {
                std::fill(B->_ia + iPrev, B->_ia + i + 1, nnz);
                for(const std::pair<unsigned int, K>& p : tmp[k]) {
                    B->_ja[nnz] = p.first;
                    B->_a[nnz++] = p.second;
                }
                ++k;
                iPrev = i + 1;
            }
            std::fill(B->_ia + iPrev, B->_ia + Subdomain<K>::_dof + 1, nnz);
        }
        /* Function: solveGEVP
         *
         *  Solves the generalized eigenvalue problem Ax = l Bx.
         *
         * Parameters:
         *    A              - Left-hand side matrix.
         *    B              - Right-hand side matrix (optional).
         *    nu             - Number of eigenvectors requested.
         *    threshold      - Precision of the eigensolver. */
        template<template<class> class Eps>
        void solveGEVP(MatrixCSR<K>* const& A, unsigned short& nu, const typename Wrapper<K>::ul_type& threshold, MatrixCSR<K>* const& B = nullptr, const MatrixCSR<K>* const& pattern = nullptr) {
            Eps<K> evp(threshold, Subdomain<K>::_dof, nu);
            bool free = pattern ? pattern->sameSparsity(A) : Subdomain<K>::_a->sameSparsity(A);
            MatrixCSR<K>* rhs = nullptr;
            if(B)
                rhs = B;
            else
                scaleIntoOverlap(A, rhs);
            evp.template solve<Solver>(A, rhs, super::_ev, Subdomain<K>::_communicator, free ? &(super::_s) : nullptr);
            if(rhs != B)
                delete rhs;
            if(free) {
                A->_ia = nullptr;
                A->_ja = nullptr;
            }
            (*Option::get())["geneo_nu"] = nu = evp.getNu();
            const int n = Subdomain<K>::_dof;
            std::for_each(super::_ev, super::_ev + nu, [&](K* const v) { std::replace_if(v, v + n, [](K x) { return std::abs(x) < 1.0 / (HPDDM_EPS * HPDDM_PEN); }, K()); });
        }
        template<bool sorted = true, bool scale = false>
        void interaction(std::vector<const MatrixCSR<K>*>& blocks) const {
            Subdomain<K>::template interaction<'C', sorted, scale>(blocks, _d);
        }
        /* Function: GMV
         *
         *  Computes a global sparse matrix-vector product.
         *
         * Parameters:
         *    in             - Input vector.
         *    out            - Output vector. */
        void GMV(const K* const in, K* const out, const unsigned short& mu = 1) const {
#if 0
            K* tmp = new K[mu * Subdomain<K>::_dof];
            Wrapper<K>::diag(Subdomain<K>::_dof, mu, _d, in, tmp);
            int dim = mu;
            Wrapper<K>::template csrmm<'C'>(Subdomain<K>::_a->_sym, &(Subdomain<K>::_dof), &dim, Subdomain<K>::_a->_a, Subdomain<K>::_a->_ia, Subdomain<K>::_a->_ja, in, out);
            delete [] tmp;
            Subdomain<K>::exchange(out, m);
#else
            int dim = mu;
            Wrapper<K>::template csrmm<'C'>(Subdomain<K>::_a->_sym, &(Subdomain<K>::_dof), &dim, Subdomain<K>::_a->_a, Subdomain<K>::_a->_ia, Subdomain<K>::_a->_ja, in, out);
            Wrapper<K>::diag(Subdomain<K>::_dof, mu, _d, out);
            Subdomain<K>::exchange(out, mu);
#endif
        }
        /* Function: computeError
         *
         *  Computes the Euclidean norm of a right-hand side and of the difference between a solution vector and a right-hand side.
         *
         * Parameters:
         *    x              - Solution vector.
         *    f              - Right-hand side.
         *    storage        - Array to store both values.
         *
         * See also: <Schur::computeError>. */
        void computeError(const K* const x, const K* const f, typename Wrapper<K>::ul_type* const storage, const unsigned short& mu = 1) const {
            int dim = mu * Subdomain<K>::_dof;
            K* tmp = new K[dim];
            GMV(x, tmp, mu);
            Wrapper<K>::axpy(&dim, &(Wrapper<K>::d__2), f, &i__1, tmp, &i__1);
            std::fill_n(storage, 2 * mu, 0.0);
            for(unsigned int i = 0; i < Subdomain<K>::_dof; ++i) {
                bool isBoundaryCond = true;
                unsigned int stop;
                if(!Subdomain<K>::_a->_sym)
                    stop = std::distance(Subdomain<K>::_a->_ja, std::upper_bound(Subdomain<K>::_a->_ja + Subdomain<K>::_a->_ia[i], Subdomain<K>::_a->_ja + Subdomain<K>::_a->_ia[i + 1], i));
                else
                    stop = Subdomain<K>::_a->_ia[i + 1];
                if(std::abs(Subdomain<K>::_a->_a[stop - 1]) > HPDDM_EPS * HPDDM_PEN)
                    continue;
                for(unsigned int j = Subdomain<K>::_a->_ia[i]; j < stop && isBoundaryCond; ++j) {
                    if(i != Subdomain<K>::_a->_ja[j] && std::abs(Subdomain<K>::_a->_a[j]) > HPDDM_EPS)
                        isBoundaryCond = false;
                    else if(i == Subdomain<K>::_a->_ja[j] && std::abs(Subdomain<K>::_a->_a[j] - K(1.0)) > HPDDM_EPS)
                        isBoundaryCond = false;
                }
                for(unsigned short nu = 0; nu < mu; ++nu) {
                    if(!isBoundaryCond)
                        storage[2 * nu + 1] += _d[i] * std::norm(tmp[nu * Subdomain<K>::_dof + i]);
                    if(std::abs(f[nu * Subdomain<K>::_dof + i]) > HPDDM_EPS * HPDDM_PEN)
                        storage[2 * nu] += _d[i] * std::norm(f[nu * Subdomain<K>::_dof + i] / K(HPDDM_PEN));
                    else
                        storage[2 * nu] += _d[i] * std::norm(f[nu * Subdomain<K>::_dof + i]);
                }
            }
            delete [] tmp;
            MPI_Allreduce(MPI_IN_PLACE, storage, 2 * mu, Wrapper<typename Wrapper<K>::ul_type>::mpi_type(), MPI_SUM, Subdomain<K>::_communicator);
            std::for_each(storage, storage + 2 * mu, [](typename Wrapper<K>::ul_type& b) { b = std::sqrt(b); });
        }
        template<char N = 'C'>
        void distributedNumbering(unsigned int* const in, unsigned int& first, unsigned int& last, unsigned int& global) const {
            Subdomain<K>::template globalMapping<N>(in, in + Subdomain<K>::_dof, first, last, global, _d);
        }
        bool distributedCSR(unsigned int* const num, unsigned int first, unsigned int last, int*& ia, int*& ja, K*& c) const {
            return Subdomain<K>::distributedCSR(num, first, last, ia, ja, c, Subdomain<K>::_a);
        }
};
} // HPDDM
#endif // _SCHWARZ_
