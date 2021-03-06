#include "Functions.h"
#include "TopOpt.h"

using namespace std;
typedef unsigned int uint;

PetscErrorCode Volume::Function( TopOpt *topOpt )
{
  PetscErrorCode ierr = 0;
  /// Objective
  ierr = VecSum( topOpt->V, values.data() ); CHKERRQ(ierr);
  values(0) /= topOpt->nElem;

  // Return if sensitivities aren't needed
  if (calc_gradient == PETSC_FALSE)
    return 0;

  /// Sensitivities
  // dVdrhof
  Vec dVdy;
  ierr = VecDuplicate( topOpt->dVdy, &dVdy ); CHKERRQ(ierr);
  ierr = VecPlaceArray( dVdy, gradients.data() ); CHKERRQ(ierr);
  ierr = VecCopy( topOpt->dVdy, dVdy ); CHKERRQ(ierr);
  ierr = VecScale( dVdy, 1.0/topOpt->nElem ); CHKERRQ(ierr);

  // dVdrhof*drhofdrho
  ierr = Chain_Filter( topOpt->P, dVdy ); CHKERRQ(ierr);

  ierr = VecResetArray( dVdy ); CHKERRQ(ierr);
  ierr = VecDestroy( &dVdy ); CHKERRQ(ierr);

  return 0;
}
