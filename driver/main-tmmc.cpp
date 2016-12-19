/*!
 * Perform (un/)biased multicomponent GCMC.
 *
 * \author Nathan A. Mahynski
 * \date 08/07/15
 *
 * \mainpage
 *
 * \section Dependencies
 *
 * \section Compiling
 * Run tests and run main
 *
 * Complile with -DFLUID_PHASE_SIMULATIONS if simulations are purely in the fluid phase.
 * This will allow tail corrections to be enabled which are only valid assuming a converging g(r) at large r.
 *
 * Compile with -DNETCDF_CAPABLE if netCDF libraries are installed and can be compiled against.  Certain data will be output to these arrays
 * instead of ASCII files if so.
 *
 * Tests compile with cmake and by default, do not link to netcdf libraries since they do not use them.  Everything tested in ASCII.
 *
 * \section Input
 *
 */

#include "../src/fhmc.h"

/*!
 * Usage: ./binary_name input.json
 */
int main (int argc, char * const argv[]) {
	std::cout << "Beginning simulation at " << getTimeStamp() << std::endl;

	moves usedMovesEq, usedMovesPr;
	simSystem sys = initialize (argv[1], &usedMovesEq, &usedMovesPr); // read json file and create system class

	// if restart/ exists, default to use this information to restart the simulation
	checkpoint cpt ("checkpt", 900);
    if (!cpt.hasCheckpoint) {
        setup (sys, argv[1]);
    }

	// TODO: finish adding restarting information so system knows how to pick up where it left off
	// update tests
	
	if (!cpt.walaDone) {
		// perform Wang-Landau simulation
		performWALA (sys, cpt, &usedMovesEq);
		performCrossover (sys, cpt, &usedMovesEq);
		performTMMC (sys, cpt, &usedMovesPr);
	} else if (!cpt.crossoverDone) {
		// crossover to TMMC
		performCrossover (sys, cpt, &usedMovesEq);
		performTMMC (sys, cpt, &usedMovesPr);
	} else if (!cpt.tmmcDone) {
		// perform tmmc portion of the simulation
		performTMMC (sys, cpt, &usedMovesPr);
	}

	sanityChecks(sys);

    sys.printSnapshot("final.xyz", "last configuration");
    sys.refineEnergyHistogramBounds();
    sys.printEnergyHistogram("final_eHist");
    sys.refinePkHistogramBounds();
    sys.printPkHistogram("final_pkHist");
    sys.printExtMoments("final_extMom");
    sys.getTMMCBias()->print("final", false);

    std::cout << "Finished simulation at " << getTimeStamp() << std::endl;
	return SAFE_EXIT;
}
