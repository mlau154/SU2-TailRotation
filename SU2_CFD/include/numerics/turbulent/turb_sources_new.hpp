﻿/*!
 * \file turb_sources.hpp
 * \brief Delarations of numerics classes for integration of source
 *        terms in turbulence problems.
 * \author F. Palacios, T. Economon
 * \version 7.2.1 "Blackbird"
 *
 * SU2 Project Website: https://su2code.github.io
 *
 * The SU2 Project is maintained by the SU2 Foundation
 * (http://su2foundation.org)
 *
 * Copyright 2012-2021, SU2 Contributors (cf. AUTHORS.md)
 *
 * SU2 is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * SU2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with SU2. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "../scalar/scalar_sources.hpp"

/*!
 * \class CSourcePieceWise_TurbSA
 * \brief Class for integrating the source terms of the Spalart-Allmaras turbulence model equation.
 * \brief The variables that are subject to change in each variation/correction have their own class. Additional source
 * terms are implemented as decorators. \ingroup SourceDiscr \author A. Bueno.
 */
template <class FlowIndices, class Omega_class, class ft2_class, class ModVort_class, class r_class, class SourceTerms_class>
class CSourceBase_TurbSA : public CNumerics {
 protected:

   /* For performance we should cut down on "aux" class variables.
    * Such variables are now declared in the struct CommonVariables
    * which can then be a local variable in ComputeResidual.
    */

  su2double Gamma_BC = 0.0;
  su2double intermittency;

  /*--- Source term components ---*/
  su2double Production, Destruction, CrossProduction, AddSourceTerm;

  /*--- Residual and Jacobian ---*/
  su2double Residual, *Jacobian_i;

 private:
  const FlowIndices idx; /*!< \brief Object to manage the access to the flow primitives. */

  unsigned short iDim;

  su2double Jacobian_Buffer;  /// Static storage for the Jacobian (which needs to be pointer for return type).

 protected:
  const bool rotating_frame = false;
  bool roughwall = false;

  bool transition;
  bool axisymmetric;

 public:
  /*!
   * \brief Constructor of the class.
   * \param[in] val_nDim - Number of dimensions of the problem.
   * \param[in] val_nVar - Number of variables of the problem.
   * \param[in] config - Definition of the particular problem.
   */
  CSourceBase_TurbSA(unsigned short val_nDim, unsigned short val_nVar, const CConfig* config)
      : CNumerics(val_nDim, val_nVar, config), idx(val_nDim, config->GetnSpecies()) {}

  /*!
   * \brief Residual for source term integration.
   * \param[in] intermittency_in - Value of the intermittency.
   */
  inline void SetIntermittency(su2double intermittency_in) final { intermittency = intermittency_in; }

  /*!
   * \brief Residual for source term integration.
   * \param[in] val_production - Value of the Production.
   */
  inline void SetProduction(su2double val_production) final { Production = val_production; }

  /*!
   * \brief Residual for source term integration.
   * \param[in] val_destruction - Value of the Destruction.
   */
  inline void SetDestruction(su2double val_destruction) final { Destruction = val_destruction; }

  /*!
   * \brief Residual for source term integration.
   * \param[in] val_crossproduction - Value of the CrossProduction.
   */
  inline void SetCrossProduction(su2double val_crossproduction) final { CrossProduction = val_crossproduction; }

  /*!
   * \brief ______________.
   */
  inline su2double GetProduction(void) const final { return Production; }

  /*!
   * \brief  Get the intermittency for the BC trans. model.
   * \return Value of the intermittency.
   */
  inline su2double GetGammaBC(void) const final { return Gamma_BC; }

  /*!
   * \brief  ______________.
   */
  inline su2double GetDestruction(void) const final { return Destruction; }

  /*!
   * \brief  ______________.
   */
  inline su2double GetCrossProduction(void) const final { return CrossProduction; }

  /*!
   * \brief Residual for source term integration.
   * \param[in] config - Definition of the particular problem.
   * \return A lightweight const-view (read-only) of the residual/flux and Jacobians.
   */
  ResidualType<> ComputeResidual(const CConfig* config) final {

    /*--- Model common auxiliary and constant variables ---*/
    CommonVariables model_var;

    // Set the boolean here depending on whether the point is closest to a rough wall or not.
    roughwall = (roughness_i > 0.0);

    Density_i = V_i[idx.Density()];
    Laminar_Viscosity_i = V_i[idx.LaminarViscosity()];

    Residual = 0.0;
    Production = 0.0;
    Destruction = 0.0;
    CrossProduction = 0.0;
    AddSourceTerm = 0.0;
    Jacobian_i[0] = 0.0;

    /*--- Evaluate Omega ---*/
    Omega_class::get(model_var);

    /*--- Rotational correction term ---*/

    if (rotating_frame) {
      model_var.Omega += 2.0 * min(0.0, StrainMag_i - model_var.Omega);
    }

    if (dist_i > 1e-10) {

       /*--- Vorticity ---*/
       model_var.S = model_var.Omega;

       model_var.dist_i_2 = dist_i * dist_i;
       su2double nu = Laminar_Viscosity_i / Density_i;
       model_var.inv_k2_d2 = 1.0 / (model_var.k2 * model_var.dist_i_2);

      /*--- Modified values for roughness ---*/
      /*--- Ref: Aupoix, B. and Spalart, P. R., "Extensions of the Spalart-Allmaras Turbulence Model to Account for Wall
       * Roughness," International Journal of Heat and Fluid Flow, Vol. 24, 2003, pp. 454-462. ---*/
      /* --- See https://turbmodels.larc.nasa.gov/spalart.html#sarough for detailed explanation. ---*/
      model_var.Ji = ScalarVar_i[0] / nu + model_var.cr1 * (roughness_i / (dist_i + EPS));  // roughness_i = 0 for smooth walls and Ji remains the
                                                                                            // same, changes only if roughness is specified.nue/nul
      model_var.d_Ji = 1.0 / nu;

      const su2double Ji_2 = model_var.Ji * model_var.Ji;
      const su2double Ji_3 = Ji_2 * model_var.Ji;

      model_var.fv1 = Ji_3 / (Ji_3 + model_var.cv1_3);
      model_var.d_fv1 = 3.0 * Ji_2 * model_var.cv1_3 / (nu * pow(Ji_3 + model_var.cv1_3, 2.0));

      /*--- Using a modified relation so as to not change the Shat that depends on fv2. ---*/
      model_var.fv2 = 1.0 - ScalarVar_i[0] / (nu + ScalarVar_i[0] * model_var.fv1);  // From NASA turb modeling resource and 2003 paper
      model_var.d_fv2 = -(1.0 / nu - Ji_2 * model_var.d_fv1) / pow(1.0 + model_var.Ji * model_var.fv1, 2.0);

      /*--- Compute ft2 term ---*/
      ft2_class::get(model_var);

      /*--- Compute modified vorticity ---*/
      ModVort_class::get(model_var);

      model_var.inv_Shat = 1.0 / model_var.Shat;

      /*--- Compute auxiliary function r ---*/
      r_class::get(model_var);

      model_var.g = model_var.r + model_var.cw2 * (pow(model_var.r, 6.0) - model_var.r);
      model_var.g_6 = pow(model_var.g, 6.0);
      model_var.glim = pow((1.0 + model_var.cw3_6) / (model_var.g_6 + model_var.cw3_6), 1.0 / 6.0);
      model_var.fw = model_var.g * model_var.glim;

      model_var.d_g = model_var.d_r * (1. + model_var.cw2 * (6.0 * pow(model_var.r, 5.0) - 1.0));
      model_var.dfw = model_var.d_g * model_var.glim * (1. - model_var.g_6 / (model_var.g_6 + model_var.cw3_6));

      model_var.norm2_Grad = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        model_var.norm2_Grad += ScalarVar_Grad_i[0][iDim]*ScalarVar_Grad_i[0][iDim];

      /*--- Compute production, destruction and cross production and jacobian ---*/
      SourceTerms_class::get(model_var, &Production, &Destruction, &CrossProduction, &Jacobian_i[0]);

      /*--- Compute any necessary additional source term and jacobian contribution ---*/

      /*--- Residual ---*/
      Residual = Production - Destruction + CrossProduction + AddSourceTerm;
      Residual *= Volume;

      /*--- Jacobian ---*/
      Jacobian_i[0] *= Volume;
    }
  }
};

/*------------------------------------------------------------------------------
| Structure with SA common auxiliary functions and constants
------------------------------------------------------------------------------*/
struct CommonVariables {
   /*--- List of constants ---*/
   su2double cv1_3, k2, cb1, cw2, ct3, ct4, cw3_6, cb2_sigma, sigma, cb2, cw1, cr1;

   /*--- List of auxiliary functions ---*/
   su2double ft2, d_ft2, r, d_r, g, d_g, glim, fw, d_fw, Ji, d_Ji, S, Shat, d_Shat, fv1, d_fv1, fv2, d_fv2;

   /*--- List of helpers ---*/
   su2double Omega, dist_i_2, inv_k2_d2, inv_Shat, g_6, norm2_Grad;
};

/*------------------------------------------------------------------------------
| Strain rate
------------------------------------------------------------------------------*/

/*!
 * \brief Baseline
 */
struct Omega_Bsl {
   template <class Base>
   static void get(CommonVariables &model_var) {

      using Base::Vorticity_i;

      model_var.Omega = sqrt(Vorticity_i[0] * Vorticity_i[0] + Vorticity_i[1] * Vorticity_i[1] + Vorticity_i[2] * Vorticity_i[2]);
   }
};

/*!
 * \brief Edward
 *
 * Here Omega is the Strain Rate
 */
struct Omega_Edw {
   template <class Base>
   static void get(CommonVariables &model_var) {

      using Base::PrimVar_Grad_i;
      using Base::idx;

      unsigned short iDim, jDim;

      su2double Sbar = 0.0;
      for(iDim=0;iDim<nDim;++iDim){
        for(jDim=0;jDim<nDim;++jDim){
          Sbar+= (PrimVar_Grad_i[idx.Velocity()+iDim][jDim]+
                  PrimVar_Grad_i[idx.Velocity()+jDim][iDim]) * PrimVar_Grad_i[idx.Velocity()+iDim][jDim];
        }
      }
      for(iDim=0;iDim<nDim;++iDim){
        Sbar-= ((2.0/3.0)*pow(PrimVar_Grad_i[idx.Velocity()+iDim][iDim], 2));
      }

      model_var.Omega = sqrt(max(Sbar, 0.0));
   }
};


/*------------------------------------------------------------------------------
| ft2-term and its derivative
------------------------------------------------------------------------------*/

/*!
 * \brief SU2 baseline ft2 term value and its derivative. ft2=0.0
 */
struct ft2_Bsl {
   template <class Base>
   static void get(CommonVariables &model_var) {
      model_var.ft2 = 0.0;
      model_var.d_ft2 = 0.0;
   }
};

/*!
 * \brief non-zero ft2 term according to the literature and its derivative.
 */
struct ft2_nonzero {
   template <class Base>
   static void get(CommonVariables *model_var) {
      const su2double xsi2 = pow(model_var->Ji, 2);
      model_var->ft2  = model_var->ct3 * exp(-model_var->ct4 * xsi2);
      model_var->d_ft2 = -2.0 * model_var->ct4 * model_var->Ji * model_var->ft2 * model_var->d_Ji;
   }
};

/*------------------------------------------------------------------------------
| Modified vorticity (\tilde{S}) and its derivative
------------------------------------------------------------------------------*/

/*!
 * \brief Baseline
 *
 * Required values: S, fv2, d_fv2, inv_k2_d2
 */
struct ModVort_Bsl {
   template <class Base>
   static void get(CommonVariables &model_var) {

      su2double nue = Base::ScalarVar_i[0];

      const su2double Sbar = nue * model_var.fv2 * model_var.inv_k2_d2;

      model_var.Shat = model_var.S + Sbar;
      model_var.Shat = max(model_var.Shat, 1.0e-10);

      const su2double d_Sbar = (model_var.fv2 + nue * model_var.d_fv2) * model_var.inv_k2_d2;

      model_var.d_Shat = (model_var.Shat <= 1.0e-10) ? 0.0 : d_Sbar;
   }
};

/*!
 * \brief Edward
 *
 * Required values: S, nu, Ji, fv1, d_fv1, fv2, d_fv2, inv_k2_d2
 */
struct ModVort_Edw {
   template <class Base>
   static void get(CommonVariables &model_var) {

      model_var.Shat = max(model_var.S*((1.0/max(model_var.Ji,1.0e-16))+model_var.fv1),1.0e-16);
      model_var.Shat = max(model_var.Shat, 1.0e-10);

      model_var.d_Shat = (model_var.Shat <= 1.0e-10) ? 0.0 : -model_var.S*pow(model_var.Ji,-2.0)/nu + model_var.S*model_var.d_fv1;
   }
};

/*!
 * \brief Negative
 *
 * Required values: same as baseline model
 */
struct ModVort_Neg {
   template <class Base>
   static void get(CommonVariables &model_var) {

      su2double nue = Base::ScalarVar_i[0];

      if (nue > 0.0) {
         // Don't check whether Sbar <>= -cv2*S.
         // Ref: Steven R. Allmaras, Forrester T. Johnson and Philippe R. Spalart - "Modifications and Clarications for the Implementation of the Spalart-Allmaras Turbulence Model" eq. 12
         // Baseline solution
         ModVort_Bsl::get<Base>(model_var);
      }
      //    else {
      //      // No need for Sbar
      //    }
   }
};

/*------------------------------------------------------------------------------
| Auxiliary function r and its derivative.
------------------------------------------------------------------------------*/

/*!
 * \brief Baseline
 *
 * Requires: Shat, d_Shat, inv_Shat, inv_k2_d2
 */
struct r_Bsl {
   template <class Base>
   static void get(CommonVariables &model_var) {

      su2double nue = Base::ScalarVar_i[0];

      model_var.r = min(nue * model_var.inv_Shat * model_var.inv_k2_d2, 10.0);
      model_var.d_r = (model_var.Shat - nue * model_var.d_Shat) * model_var.inv_Shat * model_var.inv_Shat * model_var.inv_k2_d2;
      if (model_var.r == 10.0) model_var.d_r = 0.0;
   }
};

/*!
 * \brief Edward
 *
 * Requires: Shat, d_Shat, inv_Shat, inv_k2_d2
 */
struct r_Edw {
   template <class Base>
   static void get(CommonVariables &model_var) {

      su2double nue = Base::ScalarVar_i[0];

      model_var.r = min(nue * model_var.inv_Shat * model_var.inv_k2_d2, 10.0);
      model_var.r = tanh(model_var.r) / tanh(1.0);

      model_var.d_r = (model_var.Shat - nue * model_var.d_Shat) * model_var.inv_Shat * model_var.inv_Shat * model_var.inv_k2_d2;
      model_var.d_r = (1 - pow(tanh(model_var.r), 2.0)) * (model_var.d_r) / tanh(1.0);
   }
};

/*------------------------------------------------------------------------------
| Compute source terms: production, destruction and cross-productions term and their derivatives.
------------------------------------------------------------------------------*/

/*!
 * \brief Baseline (Original SA model)
 */
struct SourceTerms_Bsl {
   template <class Base>
   static void get(CommonVariables &model_var, su2double* Production, su2double* Destruction, su2double* CrossProduction, su2double* Jacobian) {

      su2double nue = Base::ScalarVar_i[0];

      ComputeProduction(nue, model_var, Production, Jacobian);

      ComputeDestruction(nue, model_var, Destruction, Jacobian);

      ComputeCrossProduction(nue, model_var, CrossProduction, Jacobian);
   }

   static void ComputeProduction(const su2double nue, const CommonVariables &model_var, su2double* Production, su2double* Jacobian) {
      *Production = model_var.cb1 * (1.0 - model_var.ft2) * model_var.Shat * nue;
      *Jacobian  += model_var.cb1 * (-model_var.Shat * nue * model_var.d_ft2 + (1.0 - model_var.ft2) * (nue * model_var.d_Shat + model_var.Shat));
   }

   static void ComputeDestruction(const su2double nue, const CommonVariables &model_var, su2double* Destruction, su2double* Jacobian) {
      *Destruction = (model_var.cw1 * model_var.fw - model_var.cb1 * model_var.ft2 / model_var.k2) * nue * nue / model_var.dist_i_2;
      *Jacobian   -= (model_var.cw1 * model_var.d_fw - model_var.cb1 / model_var.k2 * model_var.d_ft2) * nue * nue / model_var.dist_i_2 + (model_var.cw1 * model_var.fw - model_var.cb1 * model_var.ft2 / model_var.k2) * 2.0 * nue / model_var.dist_i_2;
   }

   static void ComputeCrossProduction(const su2double nue, const CommonVariables &model_var, su2double* CrossProduction, su2double* Jacobian) {
      *CrossProduction = model_var.cb2_sigma * model_var.norm2_Grad;
      /*--- No contribution to the jacobian ---*/
   }


};

/*!
 * \brief Negative
 */
struct SourceTerms_Neg {
   template <class Base>
   static void get(CommonVariables &model_var, su2double* Production, su2double* Destruction, su2double* CrossProduction, su2double* Jacobian) {

      su2double nue = Base::ScalarVar_i[0];

      if (nue > 0.0) {

         // Baseline solution
         SourceTerms_Bsl::get<Base>(model_var, Production, Destruction, CrossProduction, Jacobian);

      } else {

         ComputeProduction(nue, model_var, Production, Jacobian);

         ComputeDestruction(nue, model_var, Destruction, Jacobian);

         ComputeCrossProduction(nue, model_var, CrossProduction, Jacobian);

      }
   }

   static void ComputeProduction(const su2double nue, const CommonVariables &model_var, su2double* Production, su2double* Jacobian) {
      *Production = model_var.cb1 * (1.0 - model_var.ct3) * model_var.S * nue;
      *Jacobian  += model_var.cb1 * (1.0 - model_var.ct3) * model_var.S;
   }

   static void ComputeDestruction(const su2double nue, const CommonVariables &model_var, su2double* Destruction, su2double* Jacobian) {
      *Destruction = model_var.cw1 * nue * nue / model_var.dist_i_2;
      *Jacobian   -= 2.0 * model_var.cw1 * nue / model_var.dist_i_2;
   }

   static void ComputeCrossProduction(const su2double nue, const CommonVariables &model_var, su2double* CrossProduction, su2double* Jacobian) {
      /*--- Same cross production as baseline. No need to duplicate code. ---*/
      SourceTerms_Bsl::ComputeCrossProduction(nue, model_var, CrossProduction, Jacobian);
   }
};

/*------------------------------------------------------------------------------
| Additional source terms
------------------------------------------------------------------------------*/

///*!
// * \brief Baseline
// */
//template <class Base>
//class CrossProduction_Bsl {
//  using Base::cb2_sigma;
//  using Base::norm2_Grad;

//  static constexpr su2double nue = Base::ScalarVar_i[0];

//  static double get(su2double* CrossProd, su2double* d_CrossProd) {
//    *CrossProd = cb2_sigma * norm2_Grad;

//    // No cross production influence in the Jacobian
//    *d_CrossProd = 0.0;
//  }
//};
