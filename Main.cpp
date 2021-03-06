#include <iostream>
#include <fstream>
#include <Eigen/Eigen>
#include <sstream>
#include "TopOpt.h"
#include "EigLab.h"
#include "MMA.h"
#include "EigenPeetz.h"
#include <slepceps.h>
#include "Functions.h"

static char help[] = "The topology optimization routine we deserve, but not the one we need right now.\n\n";

#define PETSC_ERR_PRINT_STEP 101

int main(int argc, char **args)
{
    /// MPI Variables
    int myid, nproc;
    PetscErrorCode ierr = 0;
    SlepcInitialize(&argc,&args,(char*)0,help);
    ierr = EigenPeetz::Initialize(); CHKERRQ(ierr);
    MPI_Comm Opt_Comm = MPI_COMM_WORLD;
    MPI_Comm_rank(Opt_Comm, &myid);
    MPI_Comm_size(Opt_Comm, &nproc);

    /// Optimization Variables
    TopOpt * topOpt = new TopOpt;
    MMA * optmma = new MMA;
    optmma->Set_Comm(Opt_Comm);

    /// Input Parameters
    topOpt->filename = "Standard_Input";
    char input[256]; PetscBool hasInput;
    ierr = PetscOptionsGetString(NULL, NULL, "-Input", input, 256, &hasInput); CHKERRQ(ierr);
    if (hasInput)
      topOpt->filename = input;

    Eigen::VectorXd Dimensions;
    ArrayXPI Nel;
    double R;

    bool Normalization = false, Reorder_Mesh = true;
    PetscInt mg_levels = 2, min_size = -1;
    ierr = topOpt->Def_Param(optmma, Dimensions, Nel, R, Normalization,
                      Reorder_Mesh, mg_levels, min_size); CHKERRQ(ierr);
    mg_levels = std::max(mg_levels, 2);
    ierr = topOpt->Set_Funcs(); CHKERRQ(ierr);
    ierr = topOpt->Get_CL_Options(); CHKERRQ(ierr);

    /// Domain, Boundary Conditions, and initial design variables
    Eigen::VectorXd xIni;
    if (topOpt->folder.length() > 0)
    {
      ierr = topOpt->LoadMesh(xIni); CHKERRQ(ierr);
    }
    else
    {
      ierr = topOpt->CreateMesh(Dimensions, Nel, R, Reorder_Mesh, 
                                mg_levels, min_size); CHKERRQ(ierr);
      topOpt->Def_BC();
      xIni = 0.5*Eigen::VectorXd::Ones(topOpt->nLocElem);
      topOpt->penal = topOpt->pmin;
    }

    // Write out the mesh to file
    ierr = topOpt->MeshOut(); CHKERRQ(ierr);

    /// Design Variable Initialization
    optmma->Set_Lower_Bound( Eigen::VectorXd::Constant(topOpt->nLocElem, 0) );
    optmma->Set_Upper_Bound( Eigen::VectorXd::Ones(topOpt->nLocElem) );
    optmma->Set_Values( xIni );
    topOpt->active = Eigen::Array<bool, -1, 1>::Ones(topOpt->nLocElem);
    optmma->Set_n( topOpt->nLocElem );

    /// Initialze functions and FEM structures
    std::cout.precision(12);
    topOpt->FEInitialize();
    PetscInt ncon = 0;
    for (unsigned int ii = 0; ii < topOpt->function_list.size(); ii++)
    {
      if (topOpt->function_list[ii]->objective == PETSC_FALSE)
        ncon++;
    }
    double f;
    VectorXPS dfdx(topOpt->nLocElem), g(ncon);
    MatrixXPS dgdx(topOpt->nLocElem, ncon);
    topOpt->bucklingShape.resize(topOpt->node.size(), topOpt->bucklingShape.cols());
    topOpt->dynamicShape.resize(topOpt->node.size(), topOpt->dynamicShape.cols());
    for (unsigned int i = 0; i < topOpt->function_list.size(); i++){
      ierr = topOpt->function_list[i]->Initialize_Arrays(topOpt->nLocElem); CHKERRQ(ierr); }

    /// Optimize
    for ( ; topOpt->penal <= topOpt->pmax; topOpt->penal += topOpt->pstep )
    {
      ierr = PetscFPrintf(topOpt->comm, topOpt->output, "\nPenalty increased to %1.3g\n",
                  topOpt->penal); CHKERRQ(ierr);

      optmma->Set_It(0);
      ierr = topOpt->MatIntFnc( optmma->Get_x() ); CHKERRQ(ierr);
      ierr = PetscLogEventBegin(topOpt->FEEvent, 0, 0, 0, 0); CHKERRQ(ierr);
      if (topOpt->needK)
      {
        ierr = topOpt->FEAssemble(); CHKERRQ(ierr);
      }
      if (topOpt->needU)
      {
        ierr = topOpt->FESolve(); CHKERRQ(ierr);
      }
      ierr = PetscLogEventEnd(topOpt->FEEvent, 0, 0, 0, 0); CHKERRQ(ierr);
      ierr = PetscLogEventBegin(topOpt->funcEvent, 0, 0, 0, 0); CHKERRQ(ierr);
      ierr = Function_Base::Function_Call( topOpt, f, dfdx, g, dgdx ); CHKERRQ(ierr);
      ierr = PetscLogEventEnd(topOpt->funcEvent, 0, 0, 0, 0); CHKERRQ(ierr);
      ierr = topOpt->StepOut(f, g, optmma->Get_It(), optmma->Get_nactive());
                CHKERRQ(ierr);

  /*ierr = PetscFClose(topOpt->comm, topOpt->output); CHKERRQ(ierr);
  delete topOpt;
  delete optmma;
  ierr = SlepcFinalize(); CHKERRQ(ierr);
  return ierr;*/

      do
      {
        ierr = PetscLogEventBegin(topOpt->UpdateEvent, 0, 0, 0, 0); CHKERRQ(ierr);
        ierr = optmma->Set_Active(topOpt->active); CHKERRQ(ierr);
        ierr = optmma->Update( dfdx, g, dgdx ); CHKERRQ(ierr);
        ierr = PetscLogEventEnd(topOpt->UpdateEvent, 0, 0, 0, 0); CHKERRQ(ierr);
        ierr = topOpt->MatIntFnc( optmma->Get_x() ); CHKERRQ(ierr);
        ierr = PetscLogEventBegin(topOpt->FEEvent, 0, 0, 0, 0); CHKERRQ(ierr);
        if (topOpt->needK)
        {
          ierr = topOpt->FEAssemble(); CHKERRQ(ierr);
        }
        if (topOpt->needU)
        {
          ierr = topOpt->FESolve(); CHKERRQ(ierr);
        }
        ierr = PetscLogEventEnd(topOpt->FEEvent, 0, 0, 0, 0); CHKERRQ(ierr);
        ierr = PetscLogEventBegin(topOpt->funcEvent, 0, 0, 0, 0); CHKERRQ(ierr);
        ierr = Function_Base::Function_Call( topOpt, f, dfdx, g, dgdx ); CHKERRQ(ierr);
        ierr = PetscLogEventEnd(topOpt->funcEvent, 0, 0, 0, 0); CHKERRQ(ierr);

        ierr = topOpt->StepOut(f, g, optmma->Get_It()+1, optmma->Get_nactive());
                  CHKERRQ(ierr);

      } while ( !optmma->Check() );

      /// Print result after this penalization
      ierr = topOpt->ResultOut(optmma->Get_It()); CHKERRQ(ierr);
    }

    /// Print out all function values if desired
    if (Normalization)
    {
      ierr = PetscFPrintf(topOpt->comm, topOpt->output, "***Final Values***\n"); CHKERRQ(ierr);
      ierr = Function_Base::Normalization(topOpt); CHKERRQ(ierr);
    }

    /// Wrap up and finish
    delete topOpt;
    delete optmma;

    ierr = EigenPeetz::Finalize(); CHKERRQ(ierr);
    ierr = SlepcFinalize(); CHKERRQ(ierr);

    return ierr;
}

