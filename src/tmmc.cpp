#include "tmmc.h"

/*!
 * Perform TMMC stage of simulation
 *
 * \param [in] sys System to simulate
 * \param [in] res Restart/checkpoint information
 * \param [in] usedMovesPr Move class to use
 */
void performTMMC (simSystem &sys, checkpoint &res, moves *usedMovesPr) {
    if (res.tmmcDone) {
        throw customException ("Checkpoint indicates TMMC already finished");
    }
    std::cout << "Beginning TMMC at " << getTimeStamp() << std::endl;

    // Specifically for printing progress every 1% of the simulation
    const long long int numSweepSnaps = 100;
	unsigned long long int sweepPrint = sys.totalTMMCSweeps, printCounter = 0;
	if (sys.totalTMMCSweeps > numSweepSnaps) {
		sweepPrint /= numSweepSnaps;
	}

    res.tmmcDone = false;
    unsigned long long int sweep = 0;
    unsigned long long int counter = 0;
    if (!res.resFromTMMC) {
        if (sys.useWALA) {
            throw customException ("WALA not deactivated, cannot proceeed with TMMC");
        }
        if (sys.restartFromTMMC) {
            if (!sys.useTMMC) {
                // In case where not restarting from checkpoint, but starting fresh from TMMC, crossover stage was skipped so TMMC has not been activated yet
                sys.startTMMC (sys.tmmcSweepSize, sys.getTotalM());
            }
    		try {
    			sys.getTMMCBias()->readC(sys.restartFromTMMCFile); // read collection matrix
    			sys.getTMMCBias()->calculatePI();
    	        std::cout << "Restarted TMMC from collection matrix from " << sys.restartFromTMMCFile << std::endl;
    		} catch (customException &ce) {
    			sys.stopTMMC(); // deallocate
    			std::cerr << "Failed to initialize from TMMC collection matrix: " << ce.what() << std::endl;
    			exit(SYS_FAILURE);
    		}
    	}
    } else {
        printCounter = res.moveCounter;
        sweep = res.sweepCounter;
    }

    std::cout << "Starting progress stage from " << printCounter << "/" << std::min(numSweepSnaps, sys.totalTMMCSweeps) << " at " << getTimeStamp() << std::endl;
    std::cout << "Starting from " << sweep << "/" << sys.totalTMMCSweeps << " total TMMC sweeps at " << getTimeStamp() << std::endl;

    unsigned long long int checkPoint = sys.tmmcSweepSize*(sys.totNMax() - sys.totNMin() + 1)*3; // how often to check full traversal of collection matrix
	while (sweep < sys.totalTMMCSweeps) {
		bool done = false;

		// Perform a sweep
		while (!done) {
			try {
				usedMovesPr->makeMove(sys);
			} catch (customException &ce) {
				std::cerr << ce.what() << std::endl;
				exit(SYS_FAILURE);
			}

			// Only record properties of the system when it is NOT in an intermediate state
			if (sys.getCurrentM() == 0) {
				sys.recordEnergyHistogram();
				sys.recordPkHistogram();
				sys.recordExtMoments();
			}

			// Check if sweep is done
			if (counter%checkPoint == 0) {
				done = sys.getTMMCBias()->checkFullyVisited();
				counter = 0;
			}

			counter++;
            res.check(sys, printCounter, sweep);
		}

		sys.getTMMCBias()->iterateForward(); // reset the counting matrix and increment total sweep number
		sweep++;

		std::cout << "Finished " << sweep << "/" << sys.totalTMMCSweeps << " total TMMC sweeps at " << getTimeStamp() << std::endl;

		// Update biasing function from collection matrix
		sys.getTMMCBias()->calculatePI();

		// Periodically write out checkpoints to monitor convergence properties later - all are used in FHMCAnalysis at this point (12/22/16)
		if (sweep%sweepPrint == 0) {
			printCounter++;
			sys.getTMMCBias()->print("tmmc-Checkpoint-"+std::to_string(printCounter), false, false); //true, false);
			sys.refineEnergyHistogramBounds();
			sys.printEnergyHistogram("eHist-Checkpoint-"+std::to_string(printCounter));
            sys.refinePkHistogramBounds();
            sys.printPkHistogram("pkHist-Checkpoint-"+std::to_string(printCounter));
            sys.printExtMoments("extMom-Checkpoint-"+std::to_string(printCounter));
            usedMovesPr->print("tmmc.stats");
		}
	}

    // Print final results
    sys.getTMMCBias()->print("final", false, false);
    sys.refineEnergyHistogramBounds();
    sys.printEnergyHistogram("final_eHist");
    sys.refinePkHistogramBounds();
    sys.printPkHistogram("final_pkHist");
    sys.printExtMoments("final_extMom");
    sys.printSnapshot("final.xyz", "last configuration");
    usedMovesPr->print("tmmc.stats");

    sanityChecks(sys);
    res.tmmcDone = true;
}
