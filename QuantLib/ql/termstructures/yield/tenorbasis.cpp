/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2015 Ferdinando Ametrano
 Copyright (C) 2015 Paolo Mazzocchi

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <ql/termstructures/yield/tenorbasis.hpp>
#include <ql/indexes/iborindex.hpp>

using boost::shared_ptr;
using std::vector;

namespace QuantLib {

    TenorBasis::TenorBasis(Date settlementDate,
                           shared_ptr<IborIndex> iborIndex,
                           const Handle<YieldTermStructure>& baseCurve)
    : settlementDate_(settlementDate), index_(iborIndex),
      baseCurve_(baseCurve) {
        // TODO: check iborIndex pointers
        // TODO: check iborIndex dayCounter
        dc_ = index_->dayCounter();
        bdc_ = index_->businessDayConvention();
        eom_ = index_->endOfMonth();
        cal_ = index_->fixingCalendar();
        tenor_ = index_->tenor();
        Date endDate = cal_.advance(settlementDate, tenor_, bdc_, eom_);
        tau_ = dc_.yearFraction(settlementDate, endDate);
        time2date_ = (endDate - settlementDate)/tau_;
    }

    Spread TenorBasis::value(Date d) const {
        Time t = timeFromSettlementDate(d);
        return value(t);
    }

    Spread TenorBasis::exactValue(Date d1) const {
        Date d2 = cal_.advance(d1, tenor_, bdc_, eom_);
        return value(d1, d2);
        //Time t1 = timeFromSettlementDate(d1);
        //Time t2 = timeFromSettlementDate(d2);
        //Real bigDelta = integrate_(t1, t2);
        //Time dt = t2 - t1;
        //// baseCurve must be a discounting curve...
        //// otherwise it could not provide fwd(t1, t2) with t2-t1!=tau_
        //Rate baseFwd = baseCurve_->forwardRate(t1, t2, Simple, Annual, false);
        //Rate fwd = ((1.0 + baseFwd*dt)*std::exp(bigDelta) - 1.0) / dt;
        //return fwd - baseFwd;
    }

    Rate TenorBasis::forwardRate(Date d1) const {
        Rate basis = value(d1);
        Date d2 = cal_.advance(d1, tenor_, bdc_, eom_);
        // baseCurve must be a discounting curve...
        // otherwise it could not provide fwd(d1, d2) with d2-d1!=tau
        Rate baseFwd = baseCurve_->forwardRate(d1, d2, dc_, Simple, Annual, 0);
        return baseFwd + basis;
    }

    Rate TenorBasis::forwardRate(Time t1) const {
        // we need Date algebra to calculate d2
        Date d1 = dateFromTime(t1);
        return forwardRate(d1);
    }

    Real TenorBasis::value(Date d1,
                           Date d2) const {
        Time t1 = timeFromSettlementDate(d1);
        Time t2 = timeFromSettlementDate(d2);
        return value(t1, t2);
    }

    Real TenorBasis::value(Time t1, 
                           Time t2) const {
        Real bigDelta = integrate_(t1, t2);
        Time dt = t2 - t1;
        // baseCurve must be a discounting curve...
        // otherwise it could not provide fwd(t1, t2) with t2-t1!=tau_
        Rate baseFwd = baseCurve_->forwardRate(t1, t2, Simple, Annual, false);
        Rate fwd = ((1.0 + baseFwd*dt)*std::exp(bigDelta) - 1.0) / dt;
        return fwd - baseFwd;
    }

    Rate TenorBasis::syntheticRate(Date d1,
                                   Date d2) const {
        Time t1 = timeFromSettlementDate(d1);
        Time t2 = timeFromSettlementDate(d2);
        return syntheticRate(t1, t2);
    }

    Rate TenorBasis::syntheticRate(Time t1,
                                   Time t2) const {
        // baseCurve must be a discounting curve...
        // otherwise it could not provide fwd(d1, d2) with d2-d1!=tau
        Rate baseFwd = baseCurve_->forwardRate(t1, t2, Simple, Annual, false);
        return value(t1, t2) + baseFwd;
    }


    Time TenorBasis::timeFromSettlementDate(Date d) const {
        return dc_.yearFraction(settlementDate_, d);
    }

    Date TenorBasis::dateFromTime(Time t) const {
        BigInteger result =
            settlementDate_.serialNumber() + BigInteger(t*time2date_);
        if (result >= Date::maxDate().serialNumber())
            return Date::maxDate();
        return Date(result);
    }

    const shared_ptr<IborIndex>& TenorBasis::iborIndex() const {
        return index_;
    }

    const Handle<YieldTermStructure>& TenorBasis::baseCurve() const {
        return baseCurve_;
    }

    Real TenorBasis::integrate_(Date d1) const {
        Date d2 = cal_.advance(d1, tenor_, bdc_, eom_);
        return integrate_(d1, d2);
    }

    Real TenorBasis::integrate_(Date d1,
                                Date d2) const {
        Time t1 = timeFromSettlementDate(d1);
        Time t2 = timeFromSettlementDate(d2);
        return integrate_(t1, t2);
    }


    AbcdTenorBasis::AbcdTenorBasis(Date settlementDate,
                                   shared_ptr<IborIndex> iborIndex,
                                   const Handle<YieldTermStructure>& baseCurve,
                                   bool isSimple,
                                   shared_ptr<AbcdMathFunction> f)
    : TenorBasis(settlementDate, iborIndex, baseCurve) {

        if (isSimple) {
            basis_ = f;
            vector<Real> coeff = f->definiteDerivativeCoefficients(0.0, tau_);
            coeff[0] *= tau_;
            coeff[1] *= tau_;
            // unaltered c coeff[2]
            coeff[3] *= tau_;
            instBasis_ = shared_ptr<AbcdMathFunction>(new AbcdMathFunction(coeff));
        } else {
            instBasis_ = f;
            vector<Real> coeff = f->definiteIntegralCoefficients(0.0, tau_);
            coeff[0] /= tau_;
            coeff[1] /= tau_;
            // unaltered c coeff[2]
            coeff[3] /= tau_;
            basis_ = shared_ptr<AbcdMathFunction>(new AbcdMathFunction(coeff));
        }
    }

    const vector<Real>& AbcdTenorBasis::coefficients() const {
        return basis_->coefficients();
    }

    const vector<Real>& AbcdTenorBasis::instCoefficients() const {
        return instBasis_->coefficients();
    }

    Date AbcdTenorBasis::maximumLocation() const {
        Time maximumLocation = basis_->maximumLocation();
        return dateFromTime(maximumLocation);
    }

    Spread AbcdTenorBasis::maximumValue() const {
        Date d = maximumLocation();
        return TenorBasis::value(d);
    }

    Real AbcdTenorBasis::integrate_(Time t1,
                                    Time t2) const {
        return instBasis_->definiteIntegral(t1, t2);
    }


    PolynomialTenorBasis::PolynomialTenorBasis(
                                    Date settlementDate,
                                    shared_ptr<IborIndex> iborIndex,
                                    const Handle<YieldTermStructure>& baseCurve,
                                    bool isSimple,
                                    shared_ptr<PolynomialFunction> f)
    : TenorBasis(settlementDate, iborIndex, baseCurve) {

        if (isSimple) {
            basis_ = f;
            vector<Real> coeff = f->definiteDerivativeCoefficients(0.0, tau_);
            for (Size i=0; i<coeff.size(); ++i)
                coeff[i] *= tau_;
            instBasis_ = shared_ptr<PolynomialFunction>(new
                PolynomialFunction(coeff));
        } else {
            instBasis_ = f;
            vector<Real> coeff = f->definiteIntegralCoefficients(0.0, tau_);
            for (Size i=0; i<coeff.size(); ++i)
                coeff[i] /= tau_;
            basis_ = shared_ptr<PolynomialFunction>(new
                PolynomialFunction(coeff));
        }

    }

    const vector<Real>& PolynomialTenorBasis::coefficients() const {
        return basis_->coefficients();
    }

    const vector<Real>& PolynomialTenorBasis::instCoefficients() const {
        return instBasis_->coefficients();
    }

    Real PolynomialTenorBasis::integrate_(Time t1,
                                          Time t2) const {
        return instBasis_->definiteIntegral(t1, t2);
    }

}
