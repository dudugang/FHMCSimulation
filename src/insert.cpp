#include "system.h"
#include "insert.h"
#include "global.h"
#include "moves.h"
#include "atom.h"
#include <sstream>
#include <iostream>

/*!
 * Insert a particle into the system.  All other information is stored in the simSystem object.
 * \param [in] sys System object to attempt to insert a particle into.
 * \return MOVE_SUCCESS if inserted a particle, otherwise MOVE_FAILURE if did not.  Will throw exceptions if there was an error.
 */
int insertParticle::make (simSystem &sys) {
    // check if at upper bound
    int max;
    try {
        max = sys.maxSpecies(typeIndex_);
    } catch (customException &ce) {
        std::string a = "Error inserting particle: ", b = ce.what();
        throw customException (a+b);
    }
    if (sys.numSpecies[typeIndex_] >= max) {
        return MOVE_FAILURE;
    }
    
	// attempt to insert a new one
    atom newAtom;
    const std::vector < double > box = sys.box();
    double V = 1.0;
    for (unsigned int i = 0; i < box.size(); ++i) {
        newAtom.pos[i] = rng (&RNG_SEED) * box[i];
        V *= box[i];
    }
    
    // compute energy to insert
    double insEnergy = 0.0;
    for (unsigned int spec = 0; spec < sys.nSpecies(); ++spec) {
    	// get positions of neighboring atoms around newAtom
    	std::vector < std::vector < double > > neighborPositions = sys.getNeighborPositions(spec, typeIndex_, &newAtom);
        for (unsigned int i = 0; i < neighborPositions.size(); ++i) {
			try {
				insEnergy += sys.ppot[spec][typeIndex_]->energy(neighborPositions[i], newAtom.pos, box);	
			} catch (customException& ce) {
				std::string a = "Cannot insert because of energy error: ", b = ce.what();
				throw customException (a+b);
			}
        }
        // add tail correction to potential energy -- only enable for fluid phase simulations
#ifdef FLUID_PHASE_SIMULATIONS
        if (sys.ppot[spec][typeIndex_]->useTailCorrection){
			insEnergy += sys.ppot[spec][typeIndex_]->tailCorrection(sys.numSpecies[spec]/V);
		}
#endif
    }
    
    // biasing
    const double p_u = V/(sys.numSpecies[typeIndex_]+1.0)*exp(sys.beta()*(sys.mu(typeIndex_) - insEnergy));
    double bias = 1.0;
    if (sys.useTMMC && !sys.useWALA) {
    	// TMMC biasing
    	if (sys.nSpecies() == 1) {
    		// Single-component, standard TMMC
    		std::vector < int > nv (1, sys.numSpecies[typeIndex_]), nv2 (1, sys.numSpecies[typeIndex_]+1);
    		const int address1 = sys.tmmcBias->getAddress(nv), address2 = sys.tmmcBias->getAddress(nv2);
       		const double b1 = sys.tmmcBias->getBias (address1), b2 = sys.tmmcBias->getBias (address2);
       		bias = exp(b2-b1);
    	} else {
    		// Multi-component, use isothermal-isochoric method of Shen & Errington
    		exit(-1); 
    	}
    } else if (!sys.useTMMC && sys.useWALA) {
    	// Wang-Landau Biasing
    	if (sys.nSpecies() == 1) {
    	    // Single-component
    	    std::vector < int > nv (1, sys.numSpecies[typeIndex_]), nv2 (1, sys.numSpecies[typeIndex_]+1);
    	    const int address1 = sys.wlBias->getAddress(nv), address2 = sys.wlBias->getAddress(nv2);
    	    const double b1 = sys.wlBias->getBias (address1), b2 = sys.wlBias->getBias (address2);
    	    bias = exp(b2-b1);
    	} else {
    		// Multi-component, use isothermal-isochoric method of Shen & Errington
    		exit(-1); 
    	}
    } else if (sys.useTMMC && sys.useWALA) {
    	// Crossover phase where we use WL but update TMMC collection matrix
    	if (sys.nSpecies() == 1) {
    		// Single-component
    		std::vector < int > nv (1, sys.numSpecies[typeIndex_]), nv2 (1, sys.numSpecies[typeIndex_]+1);
    		const int address1 = sys.wlBias->getAddress(nv), address2 = sys.wlBias->getAddress(nv2);
    		const double b1 = sys.wlBias->getBias (address1), b2 = sys.wlBias->getBias (address2);
    		bias = exp(b2-b1);
    		sys.tmmcBias->updateC (nv, nv2, std::min(1.0, p_u));    	    
    	} else {
    		// Multi-component, use isothermal-isochoric method of Shen & Errington
    	    exit(-1);   		
    	}
    } else {
    	// No biasing
    	bias = 1.0;
    }
	// metropolis criterion
	if (rng (&RNG_SEED) < p_u*bias) {
        try {
            sys.insertAtom(typeIndex_, &newAtom);
        } catch (customException &ce) {
            std::string a = "Failed to insert atom: ", b = ce.what();
            throw customException (a+b);
        }
		sys.incrementEnergy(insEnergy);
        return MOVE_SUCCESS;
    }
    
	return MOVE_FAILURE;
}
