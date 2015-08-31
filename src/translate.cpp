#include "translate.h"

/*!
 * Translate a particle in the system.  All other information is stored in the simSystem object.
 * 
 * \param [in] sys System object to attempt to remove a particle from.
 * 
 * \return MOVE_SUCCESS if translated a particle, otherwise MOVE_FAILURE if did not.  Will throw exceptions if there was an error.
 */
int translateParticle::make (simSystem &sys) {
	bool earlyReject = false;

	// check if any exist to be translated
	if (sys.getFractionalAtomType() == typeIndex_) {
		if (sys.numSpecies[typeIndex_] == 0 && sys.getCurrentM() == 0) {
        		earlyReject = true;
    		}
	} else {
		if (sys.numSpecies[typeIndex_] == 0) {
			earlyReject = true;
		}
	}

	// updates to biasing functions must be done even if at bounds
        if (earlyReject) {
                if (sys.useWALA) {
                         sys.getWALABias()->update(sys.getTotN(), sys.getCurrentM());
                }

                if (sys.useTMMC) {
                        sys.tmmcBias->updateC (sys.getTotN(), sys.getTotN(), sys.getCurrentM(), sys.getCurrentM(), 0.0);
                }

                return MOVE_FAILURE;
        }
	
	// choose a random particle of that type
	int chosenAtom = 0;
	if (sys.getCurrentM() > 0 && sys.getFractionalAtomType() == typeIndex_) {
		// we are moving a species that has a partially inserted atom, so account for that in the choice
		chosenAtom = (int) floor(rng (&RNG_SEED) * (sys.numSpecies[typeIndex_]+1));	
	} else {
		// all atoms of this type are fully inserted
		chosenAtom = (int) floor(rng (&RNG_SEED) * sys.numSpecies[typeIndex_]);
	}
    
	// attempt to translate that one
	const std::vector < double > box = sys.box();
    double V = 1.0;
    for (unsigned int i = 0; i < box.size(); ++i) {
        V *= box[i];
    }
        
    double oldEnergy = 0.0;
    for (unsigned int spec = 0; spec < sys.nSpecies(); ++spec) {
        // get positions of neighboring atoms around chosenAtom
        std::vector < atom* > neighborAtoms = sys.getNeighborAtoms(spec, typeIndex_, &sys.atoms[typeIndex_][chosenAtom]);
    	for (unsigned int i = 0; i < neighborAtoms.size(); ++i) {
			try {
				oldEnergy += sys.ppot[spec][typeIndex_]->energy(neighborAtoms[i], &sys.atoms[typeIndex_][chosenAtom], box);
			} catch (customException& ce) {
				std::string a = "Cannot translate because of energy error: ", b = ce.what();
				throw customException (a+b);
			}
        }
        // add tail correction to potential energy
#ifdef FLUID_PHASE_SIMULATIONS
        if (sys.ppot[spec][typeIndex_]->useTailCorrection) {
        	if (!(sys.getCurrentM() > 0 && sys.getFractionalAtom () == &sys.atoms[typeIndex_][chosenAtom])) {
        		// then chosenAtom is not a partially inserted particle and tail interactions must be included
        		if (spec == typeIndex_) {
        			oldEnergy += sys.ppot[spec][typeIndex_]->tailCorrection((sys.numSpecies[spec]-1)/V);		
        		} else {
        			oldEnergy += sys.ppot[spec][typeIndex_]->tailCorrection((sys.numSpecies[spec])/V);
        		}
        	}
        }
#endif
    }
    
    // store old position and move particle along random direction in interval [-maxD_:maxD_]
    std::vector<double> oldPos = sys.atoms[typeIndex_][chosenAtom].pos;
    for (unsigned int i = 0; i< sys.atoms[typeIndex_][chosenAtom].pos.size(); ++i) {
    	sys.atoms[typeIndex_][chosenAtom].pos[i] += 2.0*maxD_*(0.5-rng (&RNG_SEED));
    	
    	// apply periodic boundary conditions
    	if (sys.atoms[typeIndex_][chosenAtom].pos[i] >= box[i]) {
    		sys.atoms[typeIndex_][chosenAtom].pos[i] -= box[i];
    	} else if (sys.atoms[typeIndex_][chosenAtom].pos[i] < 0) {
    		sys.atoms[typeIndex_][chosenAtom].pos[i] += box[i];
    	}
    }
    
    // calculate energy at new position
    double newEnergy = 0.0;
    for (unsigned int spec = 0; spec < sys.nSpecies(); ++spec) {
        // get positions of neighboring atoms around chosenAtom
        std::vector< atom* > neighborAtoms = sys.getNeighborAtoms(spec, typeIndex_, &sys.atoms[typeIndex_][chosenAtom]);
    	for (unsigned int i = 0; i < neighborAtoms.size(); ++i) {
			try {
				newEnergy += sys.ppot[spec][typeIndex_]->energy(neighborAtoms[i], &sys.atoms[typeIndex_][chosenAtom], box);
			}
			catch (customException& ce) {
				std::string a = "Cannot delete because of energy error: ", b = ce.what();
				throw customException (a+b);
			}
        }
        // add tail correction to potential energy
#ifdef FLUID_PHASE_SIMULATIONS
        if (sys.ppot[spec][typeIndex_]->useTailCorrection) {
        	if (!(sys.getCurrentM() > 0 && sys.getFractionalAtom () == &sys.atoms[typeIndex_][chosenAtom])) {
        		// then chosenAtom is not a partially inserted particle and tail interactions must be included
        		if (spec == typeIndex_) {
        			newEnergy += sys.ppot[spec][typeIndex_]->tailCorrection((sys.numSpecies[spec]-1)/V);		
        		} else {
        			newEnergy += sys.ppot[spec][typeIndex_]->tailCorrection((sys.numSpecies[spec])/V);
        		}
        	}
        }
#endif
    }
    
	// biasing
	const double p_u = exp(-sys.beta()*(newEnergy - oldEnergy));
	double bias = calculateBias(sys, sys.getTotN(), sys.getCurrentM()); // N_tot doesn't change throughout this move
	 
    // tmmc gets updated the same way, regardless of whether the move gets accepted
    if (sys.useTMMC) {
    	sys.tmmcBias->updateC (sys.getTotN(), sys.getTotN(), sys.getCurrentM(), sys.getCurrentM(), std::min(1.0, p_u)); // since the total number of atoms isn't changing, can use getTotN() as both initial and final states
    }
    
	if (rng (&RNG_SEED) < p_u*bias) {
	    try {
            sys.translateAtom(typeIndex_, chosenAtom, oldPos);
        } catch (customException &ce) {
            std::string a = "Failed to translate atom: ", b = ce.what();
            throw customException (a+b);
        }
		sys.incrementEnergy(newEnergy - oldEnergy);	
		
		// update Wang-Landau bias, if used
		if (sys.useWALA) {
			sys.getWALABias()->update(sys.getTotN(), sys.getCurrentM());
		}
			
        return MOVE_SUCCESS;
    }
    
    // if move failed, reset position
    for (unsigned int i = 0; i < sys.atoms[typeIndex_][chosenAtom].pos.size(); ++i) {
    	sys.atoms[typeIndex_][chosenAtom].pos[i] = oldPos[i];
    }
    
    // update Wang-Landau bias (even if moved failed), if used
    if (sys.useWALA) {
   		sys.getWALABias()->update(sys.getTotN(), sys.getCurrentM());
    }
    	
	return MOVE_FAILURE;
}

/*!
 * Set the maximum displacement in any single move. Should be postive number lss than half the box size.
 * 
 * \param [in] maxD Maximium displacement
 * \param [in] box Box dimensions
 */
void translateParticle::setMaxDisplacement (const double maxD, const std::vector < double > &box) {
	for (unsigned int i = 0; i < box.size(); ++i) {
		if (maxD >= box[i]/2.) {
			throw customException ("Max displacement too large");
		}
	}
	if (maxD > 0) {
		maxD_ = maxD;
	} else {
		throw customException ("Max displacement must be positive");
	}
}
