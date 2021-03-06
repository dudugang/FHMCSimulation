#include "system.h"

/*!
 * Load template atom from file.
 * Template should be a JSON file containing "nCenters" (int) and a list of vecToCenters (3D each) called "vecToCenters" labelled from 1 to nCenters (as strings).
 *
 * \param [in] filename
 */
 void simSystem::loadTemplate (const int typeIndex, const std::string filename) {
 	if (typeIndex < 0 || typeIndex >= nSpecies_) {
		throw customException ("Invalid type index ("+std::to_string(typeIndex)+"), cannot load template from "+filename);
	}

    rapidjson::Document doc;
    try {
        parseJson (filename, doc);
    } catch (std::exception &ex) {
        throw customException (ex.what());
    }

    // Check JSON contains correct members
    if (!doc.HasMember("nCenters")) throw customException("\"nCenters\" is not specified in "+filename);
    if (!doc.HasMember("vecToCenters")) throw customException("\"vecToCenters\" is not specified in "+filename);

    int nCenters = 0;
    if (!doc["nCenters"].IsInt()) {
        throw customException("\"nCenters\" is not an integer in "+filename);
    } else {
        nCenters = doc["nCenters"].GetInt();
    }

    std::vector < double > dummy (3, 0.0);
    std::vector < std::vector < double > > vecToCenters (nCenters, dummy);

    for (unsigned int i = 0; i < nCenters; ++i) {
		std::string mem = std::to_string(i+1);
        if (!doc["vecToCenters"].HasMember(mem.c_str())) {
            throw customException("\"vecToCenters\" does not specify vector for center number "+mem+" in "+filename);
        } else {
            if (doc["vecToCenters"][mem.c_str()].Size() != 3) throw customException("\"vecToCenters\" for center "+mem+" is not 3D in "+filename);

            for (unsigned int j = 0; j < 3; ++j) {
                vecToCenters[i][j] = doc["vecToCenters"][mem.c_str()][j].GetDouble();
            }
        }
    }

    // Create empty templates up to typeIndex if they don't already exists

    // Instantiate template
 }

 /*!
  * Load template atom from file, then place randomly in box with random orientation.
  *
  * \param [in] filename
  *
  * \return New, randomly placed and oriented atom
  */
  atom simSystem::makeFromTemplate (const int typeIndex) {
  	if (typeIndex < 0 || typeIndex >= nSpecies_) {
 		throw customException ("Invalid type index ("+std::to_string(typeIndex)+"), cannot make template");
 	}

 	atom newAtom = templates_[typeIndex]; // Copy constructor creates a new instance

	// Place randomly inside the box by default
	for (unsigned int i = 0; i < box_.size(); ++i) {
		newAtom.pos[i] = rng (&RNG_SEED) * box_[i];
	}

	// Rotate randomly if the atom has centers
	if (newAtom.vecToCenters.begin() != newAtom.vecToCenters.end()) {
		quaternion q; // A random orientation is picked on initialization
		newAtom.rotateCenters(q);
	}

	return newAtom;
  }

/*!
 * Increase the expanded ensemble state of the system by 1.  Accounts for the periodicity of [0, M)
 */
void simSystem::incrementMState () {
	Mcurrent_++;
	if (Mcurrent_ == Mtot_) {
		Mcurrent_ = 0;
	}
}

/*!
 * Decrease the expanded ensemble state of the system by 1.  Accounts for the periodicity of [0, M)
 */
void simSystem::decrementMState () {
	Mcurrent_--;
	if (Mcurrent_ < 0) {
		Mcurrent_ += Mtot_;
	}
}

/*!
 * Set the bounds on the total number of particles in a system.  If not set manually, this defaults to the sum of the bounds given for
 * each individual species in the system.  Therefore, for single component simulations, this is identical to [minSpecies(0), maxSpecies(0)]
 * unless otherwise set.  These bounds are intended to be used to create "windows" so that specific simulations can sample subregions
 * of [minSpecies(0), maxSpecies(0)] and be stitched together with histogram reweighting later.
 *
 * ONLY TO BE USED WHEN ORDER_PARAMETER = N_{tot}.
 *
 * However, this routine will ALSO cause the system to reevaluate its bounds.  If these total bounds are outside any individual
 * bound for each atom type, nothing will change.  However, if the upper bound for total atoms is less than an upper bound for
 * a specific species, that species will have its bounds changed to match the total maximum.  As a result sys.atoms can change so this
 * routine should be called at the beginning of a simulation, never during. The total minimum will also be checked.
 * That is, if the sum of the minimum for all species is still higher than this, an exception will be throw since the system will never
 * reach such a low density anyway.  Most likely the user has made a mistake.
 *
 * Be sure to initialize other objects, such as biases, AFTER this routine has been called since it will adjust the allowable number of
 * particles in the system.
 *
 * \param [in] bounds Vector of [min, max]
 */
void simSystem::setTotNBounds (const std::vector < int > &bounds) {
	if (bounds.size() != 2) {
		throw customException ("Bounds on total N must supplied as vector of <minN, maxN>");
	}
	if (bounds[0] < 0) {
		throw customException ("Lower bound on total particles must be > 0");
	}
	if (bounds[0] > bounds[1]) {
		throw customException ("Upper bound must be greater than or equal to lower bound for total number of particles in the system");
	}
	totNBounds_ = bounds;

	int totMin = 0;
	for (unsigned int i = 0; i < nSpecies_; ++i) {
		if (maxSpecies_[i] > totNBounds_[1]) {
			maxSpecies_[i] = totNBounds_[1];
		}
		if (maxSpecies_[i] < totNBounds_[1]) {
			throw customException ("Upper bound for species "+std::to_string(i+1)+" is lower than overall max, so cannot realize possibility of system being full of this species");
		}
		totMin += minSpecies_[i];
	}
	if (totMin > totNBounds_[0]) {
		throw customException ("Lower total N bound is lower than the sum of all individual lower bounds, region cannot be completely sampled");
	}

	// Recheck bounds and possibly resize
	int tmpTot = 0;
    for (unsigned int i = 0; i < nSpecies_; ++i) {
    	if (maxSpecies_[i] < minSpecies_[i]) {
        	throw customException ("Max species < Min species");
    	}
		try {
			atoms[i].resize(maxSpecies_[i], atom());
		} catch (std::exception &e) {
			throw customException (e.what());
		}
		// If numSpecies[i] above maxSpecies_[i] for some reason, destroy the atoms beyond bound
		if (numSpecies[i] > (int)atoms[i].size()) {
			numSpecies[i] = atoms[i].size();
		}
		tmpTot += numSpecies[i];
	}
    totN_ = tmpTot;

    // Allocate space for energy matrix - this will only be recorded when the system is within the specific window we are looking for
    // Because of implementation of Shen and Errington method, this syntax is the same for single and multicomponent systems
    long long int size = totNBounds_[1] - totNBounds_[0] + 1;

    energyHistogram_.resize(0);
    energyHistogram_lb_.resize(size, -5.0);
    energyHistogram_ub_.resize(size, 5.0);

    for (unsigned int i = 0; i < size; ++i) {
    	try {
    		dynamic_one_dim_histogram dummyHist (energyHistogram_lb_[i], energyHistogram_ub_[i], energyHistDelta_);
    		energyHistogram_.resize(i+1, dummyHist);
    	} catch (std::bad_alloc &ba) {
    		throw customException ("Out of memory for energy histogram for each Ntot");
    	}
    }

    pkHistogram_.resize(0);
    dynamic_one_dim_histogram dummyPkHist (0.0, totNBounds_[1], 1.0);
    try {
    	std::vector < dynamic_one_dim_histogram > tmp (totNBounds_[1]-totNBounds_[0]+1, dummyPkHist);
		pkHistogram_.resize(nSpecies_, tmp);
	} catch (std::bad_alloc &ba) {
		throw customException ("Out of memory for particle histogram for each Ntot");
   	}

   	// Initialize moments
	std::vector < double > lbn (6,0), ubn(6,0);
	std::vector < long long unsigned int > nbn (6,0);
	ubn[0] = nSpecies_-1;
	ubn[1] = max_order_;
	ubn[2] = nSpecies_-1;
	ubn[3] = max_order_;
	ubn[4] = max_order_;
	ubn[5] = totNBounds_[1]-totNBounds_[0];

	nbn[0] = nSpecies_;
	nbn[1] = max_order_+1;
	nbn[2] = nSpecies_;
	nbn[3] = max_order_+1;
	nbn[4] = max_order_+1;
	nbn[5] = size;

	histogram hnn (lbn, ubn, nbn);
	extensive_moments_ = hnn;
}

/*!
 * Set the bounds on the total number of particles of species 1 allowed in a system.
 *
 * ONLY TO BE USED WHEN ORDER_PARAMETER = N_{1}.
 *
 * These bounds are intended to be used to create "windows" so that specific simulations can sample subregions
 * of [minSpecies(0), maxSpecies(0)] and be stitched together with histogram reweighting later.
 *
 * Be sure to initialize other objects, such as biases, AFTER this routine has been called since it will adjust the allowable number of
 * particles in the system.
 *
 * \param [in] bounds Vector of [min, max]
 */
void simSystem::setN1Bounds (const std::vector < int > &bounds) {
	if (bounds.size() != 2) {
		throw customException ("Bounds on N_{1} must supplied as vector of <minN, maxN>");
	}
	if (bounds[0] < 0) {
		throw customException ("Lower bound on N_{1} must be > 0");
	}
	if (bounds[0] > bounds[1]) {
		throw customException ("Upper bound must be greater than or equal to lower bound for N_{1} in the system");
	}

	minSpecies_[0] = bounds[0];
	maxSpecies_[0] = bounds[1];

	// Re-compute Ntotal bounds
	totNBounds_[0] = 0;
	totNBounds_[1] = 0;
	for (unsigned int i = 0; i < nSpecies_; ++i) {
    	totNBounds_[0] += minSpecies_[i];
    	totNBounds_[1] += maxSpecies_[i];
    }

	// Recheck bounds and possibly resize
	int tmpTot = 0;
    for (unsigned int i = 0; i < nSpecies_; ++i) {
    	if (maxSpecies_[i] < minSpecies_[i]) {
        	throw customException ("Max species < Min species");
    	}
		try {
			atoms[i].resize(maxSpecies_[i], atom());
		} catch (std::exception &e) {
			throw customException (e.what());
		}
		// If numSpecies[i] above maxSpecies_[i] for some reason, destroy the atoms beyond bound
		if (numSpecies[i] > (int)atoms[i].size()) {
			numSpecies[i] = atoms[i].size();
		}
		tmpTot += numSpecies[i];
	}
    totN_ = tmpTot;

    // Allocate space for energy matrix - this will only be recorded when the system is within the specific window we are looking for
    long long int size = maxSpecies_[0] - minSpecies_[0] + 1;

    energyHistogram_.resize(0);
    energyHistogram_lb_.resize(size, -5.0);
    energyHistogram_ub_.resize(size, 5.0);

    for (unsigned int i = 0; i < size; ++i) {
    	try {
    		dynamic_one_dim_histogram dummyHist (energyHistogram_lb_[i], energyHistogram_ub_[i], energyHistDelta_);
    		energyHistogram_.resize(i+1, dummyHist);
    	} catch (std::bad_alloc &ba) {
    		throw customException ("Out of memory for energy histogram for each Ntot");
    	}
    }

    pkHistogram_.resize(0);
    dynamic_one_dim_histogram dummyPkHist (0.0, totNBounds_[1], 1.0); // For simplicity, allow to record up to N_tot,max (will be trimmed later anyway)
    try {
    	std::vector < dynamic_one_dim_histogram > tmp (size, dummyPkHist);
		pkHistogram_.resize(nSpecies_, tmp);
	} catch (std::bad_alloc &ba) {
		throw customException ("Out of memory for particle histogram for each Ntot");
   	}

   	// Initialize moments
	std::vector < double > lbn (6,0), ubn(6,0);
	std::vector < long long unsigned int > nbn (6,0);
	ubn[0] = nSpecies_-1;
	ubn[1] = max_order_;
	ubn[2] = nSpecies_-1;
	ubn[3] = max_order_;
	ubn[4] = max_order_;
	ubn[5] = maxSpecies_[0] - minSpecies_[0];

	nbn[0] = nSpecies_;
	nbn[1] = max_order_+1;
	nbn[2] = nSpecies_;
	nbn[3] = max_order_+1;
	nbn[4] = max_order_+1;
	nbn[5] = size;

	histogram hnn (lbn, ubn, nbn);
	extensive_moments_ = hnn;
}

/*!
 * Insert an atom into the system. Does all the bookkeepping behind the scenes.
 *
 * \param [in] typeIndex What type the atom is (>= 0)
 * \param [in] newAtom Pointer to new atom.  A copy is stored in the system so the original may be destroyed.
 * \param [in] override Override command that prevents the expanded ensemble state from being changed.  Used during swap moves where "insertions" are temporary.
 */
void simSystem::insertAtom (const int typeIndex, atom *newAtom, bool override) {
	if (typeIndex < nSpecies_ && typeIndex >= 0) {
		if (numSpecies[typeIndex] < maxSpecies_[typeIndex]) {
			if (Mtot_ > 1 && !override) {
        		// expanded ensemble behavior, "normal" insertion and deletion
        		if (Mcurrent_ > 0) { // further inserting an atom that already partially exists in the system
        			// ensure the system pointer is correct if currently a partially inserted atom
        			if (fractionalAtom_ != newAtom || typeIndex != fractionalAtomType_) {
        				throw customException ("Fractional atom pointer does not point to atom believed to be inserted");
        			}

	        		// increment expanded state
        			fractionalAtom_->mState++;
        			Mcurrent_++;

 	       			// check if now fully inserted
        			if (fractionalAtom_->mState == Mtot_) {
        				fractionalAtom_->mState = 0;
	        			Mcurrent_ = 0;
        				totN_++;
        				numSpecies[typeIndex]++;
        			}
        		} else {
	        		// Inserting a new atom for the first time
        			atoms[typeIndex][numSpecies[typeIndex]] = (*newAtom);

        			// Assign fractional atom
        			fractionalAtom_ = &atoms[typeIndex][numSpecies[typeIndex]];
	        		fractionalAtomType_ = typeIndex;

        			// Increment expanded state
        			fractionalAtom_->mState = 1;
        			Mcurrent_ = 1;

					// Add particle into appropriate cell lists
					for (unsigned int i = 0; i < nSpecies_; ++i) {
						if (useCellList_[typeIndex][i]) {
							cellList* cl = cellListsByPairType_[typeIndex][i];
							cl->insertParticle(&atoms[typeIndex][numSpecies[typeIndex]]); // numSpecies[typeIndex] is the number of fully inserted ones, this partially inserted one comes after that
						}
					}
        		}
        	} else if (Mtot_ > 1 && override) {
	        	// Expanded ensemble behavior, but now amidst a "swap move" rather than an actual insertion or deletion.
        		// For this, insertions involve just putting the atom "back" into the system / cellLists after being artificially completely removed

        		// Ensure we insert at the proper "end"
        		int end = numSpecies[typeIndex];
        		if (Mcurrent_ > 0 && typeIndex == fractionalAtomType_ && newAtom->mState == 0) {
        			end++; // Insert after the partially inserted one since newAtom is NOT the partial one
	        	}
        		atoms[typeIndex][end] = (*newAtom);

        		// If we just added a partially inserted/deleted particle back to the system, need to update the pointer
	        	if (atoms[typeIndex][end].mState != 0) {
        			fractionalAtom_ = &atoms[typeIndex][end];
        			fractionalAtomType_ = typeIndex;

        			// Set the system's mState back to that of the atom just inserted, iff it was the partial one
        			Mcurrent_ = atoms[typeIndex][end].mState;
        		} else {
        			totN_++; // We just added a "full" atom
        			numSpecies[typeIndex]++; // We just added a "full" atom
        		}

	        	// Put newAtom into the cell lists whatever its state
               	for (unsigned int i = 0; i < nSpecies_; ++i) {
        	        if (useCellList_[typeIndex][i]) {
                 		cellList* cl = cellListsByPairType_[typeIndex][i];
            			cl->insertParticle(&atoms[typeIndex][end]);
					}
            	}
        	} else {
	        	// Direct insertion (no expanded ensemble)
				atoms[typeIndex][numSpecies[typeIndex]] = (*newAtom);
				numSpecies[typeIndex]++;
                totN_++;

				// Add particle into appropriate cell lists
				for (unsigned int i = 0; i < nSpecies_; ++i) {
					if (useCellList_[typeIndex][i]) {
						cellList* cl = cellListsByPairType_[typeIndex][i];
						cl->insertParticle(&atoms[typeIndex][numSpecies[typeIndex] - 1]);
					}
				}
			}
		} else {
			throw customException ("Reached upper bound, cannot insert an atom of type index "+std::to_string(typeIndex));
		}
	} else {
		throw customException ("That species index does not exist, cannot insert an atom");
	}
}

/*!
 * Delete an atom from the system. Does all the bookkeepping behind the scenes.
 *
 * \param [in] typeIndex What type the atom is (>= 0)
 * \param [in] atomIndex Which atom *index* of type typeIndex to destroy (>= 0)
 * \param [in] Optional override command which allows the system to delete a particle even it goes below the minimum allowed. E.g. during a swap move.
 */
void simSystem::deleteAtom (const int typeIndex, const int atomIndex, bool override) {
	if (typeIndex < nSpecies_ && typeIndex >= 0) {
       	if ((numSpecies[typeIndex] > minSpecies_[typeIndex]) || ((numSpecies[typeIndex] == minSpecies_[typeIndex]) && (Mcurrent_ > 0)) || override) {
        	if (override) {
        		// doing a swap move
        		if (Mtot_ > 1) {
        			// expanded ensemble and not necessarily deleting the partial atom

					int end = numSpecies[typeIndex] - 1;
					if (fractionalAtomType_ == typeIndex && Mcurrent_ > 0) {
						// we are deleting a particle which has to watch out for the partial atom
						end++;
					}

					if (atoms[typeIndex][atomIndex].mState == 0) {
						// if we are removing a "full" particle, have to decrement Ntot, else not
						numSpecies[typeIndex]--;
						totN_--;
					} else {
						// but if removing the partial particle, M is affected
						Mcurrent_ = 0; // regardless of how M was originally, the partial particle is now "entirely" gone
					}

					bool replace = false;
					if (&atoms[typeIndex][end] == fractionalAtom_) {
						// then the fractional atom is about to be used to replace a "full" one
						replace = true;
					}

					// have to entirely remove the particle
					for (unsigned int i = 0; i < nSpecies_; ++i) {
						if (useCellList_[typeIndex][i]) {
							cellList* cl = cellListsByPairType_[typeIndex][i];
							cl->swapAndDeleteParticle(&atoms[typeIndex][atomIndex], &atoms[typeIndex][end]);
						}
					}

					atoms[typeIndex][atomIndex] = atoms[typeIndex][end]; // "replacement" operation

					if (replace) {
						fractionalAtom_ = &atoms[typeIndex][atomIndex];	// update the pointer if necessary
					}
        		} else {
        			// no expanded ensemble, just delete particle from appropriate cell list
                    for (unsigned int i = 0; i < nSpecies_; ++i) {
                    	if (useCellList_[typeIndex][i]) {
                    		cellList* cl = cellListsByPairType_[typeIndex][i];
                    		cl->swapAndDeleteParticle(&atoms[typeIndex][atomIndex], &atoms[typeIndex][numSpecies[typeIndex] - 1]);
                    	}
                   	}

                   	atoms[typeIndex][atomIndex] = atoms[typeIndex][numSpecies[typeIndex] - 1];    // "replacement" operation
                   	numSpecies[typeIndex]--;
                   	totN_--;
        		}
        	} else {
        		// not doing a swap move, just a "regular" deletion
        		if (Mtot_ > 1) {
        			// expanded ensemble
               		if (Mcurrent_ == 1) {
               			// when we delete this atom, it is entirely gone

               			// first ensure the system pointer is correct if currently a partially inserted atom
            			if (fractionalAtom_ != &atoms[typeIndex][atomIndex] || typeIndex != fractionalAtomType_) {
            				throw customException ("Fractional atom pointer does not point to atom belived to be inserted");
            			}

            			// decrement expanded state
            			fractionalAtom_->mState = 0;
            			Mcurrent_ = 0;

            			// since deleting partial particle, do not update Ntot, etc.
            			// however, do have to remove from cellLists
            			int end = numSpecies[typeIndex]; // includes space for the partially inserted one currently in cellList
                        for (unsigned int i = 0; i < nSpecies_; ++i) {
                        	if (useCellList_[typeIndex][i]) {
                        		cellList* cl = cellListsByPairType_[typeIndex][i];
                        		cl->swapAndDeleteParticle(&atoms[typeIndex][atomIndex], &atoms[typeIndex][end]);
                        	}
                        }

						atoms[typeIndex][atomIndex] = atoms[typeIndex][end];    // "replacement" operation

               		} else if (Mcurrent_ == 0) {
               			// have to decrement Ntot, but keep in cell lists
               			numSpecies[typeIndex]--;
               			totN_--;

            			// this is a new fractional atom
            			fractionalAtom_ = &atoms[typeIndex][atomIndex];
            			fractionalAtomType_ = typeIndex;

            			// decrement expanded state
            			fractionalAtom_->mState = Mtot_-1;
            			Mcurrent_ = Mtot_-1;
               		} else {
               			// further deleting an atom that already partially exists in the system, but remains in cell lists

               			// first ensure the system pointer is correct if currently a partially inserted atom
            			if (fractionalAtom_ != &atoms[typeIndex][atomIndex] || typeIndex != fractionalAtomType_) {
            				throw customException ("Fractional atom pointer does not point to atom belived to be inserted");
            			}

               			// decrement expanded state
               			fractionalAtom_->mState -= 1;
               			Mcurrent_ -= 1;
                	}
	        	} else {
        			// no expanded ensemble, just delete particle from appropriate cell list
                    for (unsigned int i = 0; i < nSpecies_; ++i) {
                    	if (useCellList_[typeIndex][i]) {
                    		cellList* cl = cellListsByPairType_[typeIndex][i];
                    		cl->swapAndDeleteParticle(&atoms[typeIndex][atomIndex], &atoms[typeIndex][numSpecies[typeIndex] - 1]);
                    	}
                    }

                    atoms[typeIndex][atomIndex] = atoms[typeIndex][numSpecies[typeIndex] - 1];    // "replacement" operation
                   	numSpecies[typeIndex]--;
                   	totN_--;
        		}
        	}
       	} else {
           	throw customException ("System going below minimum allowable number of atoms, cannot delete an atom of type index "+std::to_string(typeIndex));
       	}
    } else {
    	throw customException ("That species index does not exist, cannot delete an atom");
    }
}

/*!
 * Translate an atom in the system. Does all the bookkeeping behind the scenes.
 * Do nothing if there is no cell list defined for the type
 *
 * \param [in] typeIndex What type the atom is (>= 0)
 * \param [in] atomIndex Which atom *index* of type typeIndex to translate (>= 0)
 * \param [in] oldPos Old position of the atom.  The current/new position should already be stored in the atom at sys.atoms[typeIndex][atomIndex]
 */
void simSystem::translateAtom (const int typeIndex, const int atomIndex, std::vector<double> oldPos) {
	if (typeIndex < nSpecies_ && typeIndex >= 0) {
        if (atomIndex >= 0) {
        	// delete particle from appropriate cell list, move to new one
        	for (unsigned int i=0; i<nSpecies_; i++) {
            	if (useCellList_[typeIndex][i]) {
	            	cellList* cl = cellListsByPairType_[typeIndex][i];
	        	    cl->translateParticle(&atoms[typeIndex][atomIndex], oldPos);
        	    }
            }
        } else {
            throw customException ("Number of those atoms in system is out of bounds, cannot translate an atom of type index "+std::to_string(typeIndex));
        }
    } else {
		throw customException ("That species index does not exist, cannot translate the atom");
    }
}

/*!
 * Toggle KE adjustment to energy setting
 */
void simSystem::toggleKE() {
	if (toggleKE_ == false) {
		toggleKE_ = true;
	} else {
		toggleKE_ = false;
	}
}

/*
 * Destructor frees biases if used and not turned off already.
 */
simSystem::~simSystem () {
	if (useTMMC) {
		delete tmmcBias;
	}
	if (useWALA) {
		delete wlBias;
	}
}

/*!
 * Initialize the system. Sets the use of both WL and TMMC biasing to false.
 *
 * \param [in] nSpecies Number of unqiue species types to allow in the system
 * \param [in] beta Inverse temperature (1/kT)
 * \param [in] box Box dimensions [x, y, z]
 * \param [in] mu Chemical potential of each species
 * \param [in] maxSpecies Maximum number of each species to allow in the system
 * \param [in] Mtot Total number of expanded ensemble states
 * \param [in] energyHistDelta Bin width of energy histogram at each Ntot (optional, default = 10.0)
 * \param [in] max_order Maximum order to record correlations to (default = 2)
 * \param [in] op Order parameter to use for sampling (default="N_{tot}", also accepts "N_{1}")
 */
simSystem::simSystem (const unsigned int nSpecies, const double beta, const std::vector < double > box, const std::vector < double > mu, const std::vector < int > maxSpecies, const std::vector < int > minSpecies, const int Mtot, const double energyHistDelta, const int max_order, const std::string op) {
	if ((box.size() != 3) || (nSpecies != mu.size()) || (maxSpecies.size() != nSpecies) || !(op == "N_{tot}" || op == "N_{1}")) {
		throw customException ("Invalid system initialization parameters");
		exit(SYS_FAILURE);
	} else {
		nSpecies_ = nSpecies;
		maxSpecies_ = maxSpecies;
		minSpecies_ = minSpecies;
		box_ = box;
		mu_ = mu;
		beta_ = beta;
		order_param_ = op;
	}

	lnF_start = 1.0; // default for lnF_start
	lnF_end = 2.0e-18; // default for lnF_end
	toggleKE_ = false; //default, do NOT adjust energy by kinetic contribution of 3/2kT per atom (just record PE)
	totalTMMCSweeps = 0;
	wlSweepSize = 0;
	wala_g = 0.5;
	wala_s = 0.8;
	nCrossoverVisits = 5;
	walaTotalStepCounter = 0;
	crossoverTotalStepCounter = 0;
	tmmcTotalStepCounter = 0;

	if (max_order < 1){
		throw customException ("max_order must be >= 1");
	}
	max_order_ = max_order;

	if (energyHistDelta <= 0) {
		throw customException ("energyHistDelta must be > 0");
	}
	energyHistDelta_ = energyHistDelta;

	for (unsigned int i = 0; i < 3; ++i) {
		if (box_[i] <= 0) {
			throw customException ("Box dimensions must be > 0");
		}
	}

	if (Mtot < 1) {
		throw customException ("Total fractional states for expanded ensemble must be >= 1");
	}
	Mtot_ = Mtot;
	Mcurrent_ = 0; // always start from fully inserted state

	try {
		ppot.resize(nSpecies);
	} catch (std::exception &e) {
		throw customException (e.what());
	}
	for (unsigned int i = 0; i < nSpecies; ++i) {
		try {
			ppot[i].resize(nSpecies);
		} catch (std::exception &e) {
			throw customException (e.what());
		}
	}

	try {
		mass_.resize(nSpecies, 1.0);
	} catch (std::exception &e) {
		throw customException (e.what());
	}

	try {
		ppotSet_.resize(nSpecies);
	} catch (std::exception &e) {
		throw customException (e.what());
	}
	for (unsigned int i = 0; i < nSpecies; ++i) {
		try {
			ppotSet_[i].resize(nSpecies, false);
		} catch (std::exception &e) {
			throw customException (e.what());
		}
	}

	// Wall potentials for each species, if there are any?
	try {
		speciesBarriers.resize(nSpecies);
	} catch (std::exception &e) {
		throw customException (e.what());
	}

	// Prepare vectors and matrices for cell lists.
	// It is crucial to reserve the correct number of cellLists in advance
	// since cellListsByPairType uses the addresses of cellLists. Otherwise,
	// if dynamic memory reallocation takes place, the pointers do not
	// correspond to initial values anymore, causing the simulation to crash.
	cellLists_.reserve(nSpecies_*nSpecies_);

	try {
		useCellList_.resize(nSpecies);
		cellListsByPairType_.resize(nSpecies);
	} catch (std::exception &e) {
		throw customException (e.what());
	}
	for (unsigned int i = 0; i < nSpecies; ++i) {
		try {
			useCellList_[i].resize(nSpecies);
			cellListsByPairType_[i].assign(nSpecies, NULL);
		} catch (std::exception &e) {
			throw customException (e.what());
		}
	}

	totN_ = 0;
	try {
		numSpecies.resize(nSpecies, 0);
	} catch (std::exception &e) {
		throw customException (e.what());
	}

    try {
    	atoms.resize(nSpecies);
   	} catch (std::exception &e) {
    	throw customException (e.what());
    }
    for (unsigned int i = 0; i < nSpecies; ++i) {
    	if (minSpecies_[i] < 0) {
    		throw customException ("Min species < 0");
        }
        if (maxSpecies_[i] < minSpecies_[i]) {
    		throw customException ("Max species < Min species");
    	}
		try {
			atoms[i].resize(maxSpecies_[i], atom());
		} catch (std::exception &e) {
			throw customException (e.what());
		}
	}

    energy_ = 0.0;

    useTMMC = false;
    useWALA = false;

    totNBounds_.resize(2, 0);
    for (unsigned int i = 0; i < nSpecies_; ++i) {
    	totNBounds_[0] += minSpecies_[i];
    	totNBounds_[1] += maxSpecies_[i];
    }

	if (order_param_ == "N_{tot}") {
		// allocate space for average U storage matrix - Shen and Errington method implies this size is always the same for
	    // both single and multicomponent mixtures
	    long long int size = totNBounds_[1] - totNBounds_[0] + 1;
	    energyHistogram_lb_.resize(size, -5.0);
	    energyHistogram_ub_.resize(size, 5.0);
	    for (unsigned int i = 0; i < size; ++i) {
	    	energyHistogram_lb_[i] = -5.0;
	    	energyHistogram_ub_[i] = 5.0;
	    	try {
	    		dynamic_one_dim_histogram dummyHist (energyHistogram_lb_[i], energyHistogram_ub_[i], energyHistDelta_);
	    		energyHistogram_.resize(i+1, dummyHist);
	    	} catch (std::bad_alloc &ba) {
	    		throw customException ("Out of memory for energy histogram for each Ntot");
	    	}
	    }
	    pkHistogram_.resize(0);
	    dynamic_one_dim_histogram dummyPkHist (0.0, totNBounds_[1], 1.0);
		try {
			std::vector < dynamic_one_dim_histogram > tmp (totNBounds_[1]-totNBounds_[0]+1, dummyPkHist);
			pkHistogram_.resize(nSpecies_, tmp);
		} catch (std::bad_alloc &ba) {
	    	throw customException ("Out of memory for particle histogram for each Ntot");
	    }

		// initialize moments
		std::vector < double > lbn (6,0), ubn(6,0);
		std::vector < long long unsigned int > nbn (6,0);
		ubn[0] = nSpecies_-1;
		ubn[1] = max_order_;
		ubn[2] = nSpecies_-1;
		ubn[3] = max_order_;
		ubn[4] = max_order_;
		ubn[5] = totNBounds_[1]-totNBounds_[0];

		nbn[0] = nSpecies_;
		nbn[1] = max_order_+1;
		nbn[2] = nSpecies_;
		nbn[3] = max_order_+1;
		nbn[4] = max_order_+1;
		nbn[5] = size;

		histogram hnn (lbn, ubn, nbn);
		extensive_moments_ = hnn;

		restartFromWALA = false;
		restartFromTMMC = false;
		restartFromWALAFile = "";
		restartFromTMMCFile = "";
	} else if (order_param_ == "N_{1}") {
		long long int size = maxSpecies_[0] - minSpecies_[0] + 1;
	    energyHistogram_lb_.resize(size, -5.0);
	    energyHistogram_ub_.resize(size, 5.0);
	    for (unsigned int i = 0; i < size; ++i) {
	    	energyHistogram_lb_[i] = -5.0;
	    	energyHistogram_ub_[i] = 5.0;
	    	try {
	    		dynamic_one_dim_histogram dummyHist (energyHistogram_lb_[i], energyHistogram_ub_[i], energyHistDelta_);
	    		energyHistogram_.resize(i+1, dummyHist);
	    	} catch (std::bad_alloc &ba) {
	    		throw customException ("Out of memory for energy histogram for each Ntot");
	    	}
	    }
	    pkHistogram_.resize(0);
	    dynamic_one_dim_histogram dummyPkHist (0.0, totNBounds_[1], 1.0); // For simplicity, allow to record up to N_tot,max (will be trimmed later anyway)
		try {
			std::vector < dynamic_one_dim_histogram > tmp (size, dummyPkHist);
			pkHistogram_.resize(nSpecies_, tmp);
		} catch (std::bad_alloc &ba) {
	    	throw customException ("Out of memory for particle histogram for each Ntot");
	    }

		// initialize moments
		std::vector < double > lbn (6,0), ubn(6,0);
		std::vector < long long unsigned int > nbn (6,0);
		ubn[0] = nSpecies_-1;
		ubn[1] = max_order_;
		ubn[2] = nSpecies_-1;
		ubn[3] = max_order_;
		ubn[4] = max_order_;
		ubn[5] = maxSpecies_[0]-minSpecies_[0];

		nbn[0] = nSpecies_;
		nbn[1] = max_order_+1;
		nbn[2] = nSpecies_;
		nbn[3] = max_order_+1;
		nbn[4] = max_order_+1;
		nbn[5] = size;

		histogram hnn (lbn, ubn, nbn);
		extensive_moments_ = hnn;

		restartFromWALA = false;
		restartFromTMMC = false;
		restartFromWALAFile = "";
		restartFromTMMCFile = "";
	} else {
		throw customException ("Unnknown order parameter, cannot initialize system");
	}
}

/*!
 * Record the extensive moment at a given Ntot.
 */
void simSystem::recordExtMoments () {
	// Only record if in range (removes equilibration stage to get in this range, if there was any)
	/*if (totN_ >= totNBounds_[0] && totN_ <= totNBounds_[1]) {
		double val = 0.0;
		std::vector < double > coords (6,0);
		coords[5] = totN_-totNBounds_[0];
		for (unsigned int i = 0; i < nSpecies_; ++i) {
			coords[0] = i;
			for (unsigned int j = 0; j <= max_order_; ++j) {
				coords[1] = j;
				for (unsigned int k = 0; k < nSpecies_; ++k) {
					coords[2] = k;
					for (unsigned int m = 0; m <= max_order_; ++m) {
						coords[3] = m;
						for (unsigned int p = 0; p <= max_order_; ++p) {
							coords[4] = p;
							val = pow(numSpecies[i], j)*pow(numSpecies[k], m)*pow(energy_, p);
							extensive_moments_.increment (coords, val);
						}
					}
				}
			}
		}
	}*/

	// In this specific, special case we know that the lower bounds on each
	// dimension are 0, and bin widths are 1.  Here, it is much more efficient
	// to manually do the direct calculation on the unrolled array location than
	// to invoke the "rounding" operations necessary in the general case.
	// The more general case is commented out above for reference, should this
	// change.

	// NOTE: be careful if trying to use symmetry to fill in the matrix, it is
	// currently simpler to just do entire loop and do coordinates calculation
	// directly than more "clever" alternatives.
	if (order_param_ == "N_{tot}") {
		if (totN_ >= totNBounds_[0] && totN_ <= totNBounds_[1]) {
			double val = 0.0, xx = 0.0, yy = 0.0;
			long long unsigned int coords = 0;
			std::vector < long long unsigned int > widths = extensive_moments_.getWidths_();
			for (unsigned int i = 0; i < nSpecies_; ++i) {
				for (unsigned int j = 0; j <= max_order_; ++j) {
					if (j > 0) {
						xx = pow(numSpecies[i], j);
					} else {
						xx = 1.0;
					}
					for (unsigned int k = 0; k < nSpecies_; ++k) {
						for (unsigned int m = 0; m <= max_order_; ++m) {
							if (m > 0) {
								yy = pow(numSpecies[k], m);
							} else {
								yy = 1.0;
							}
							for (unsigned int p = 0; p <= max_order_; ++p) {
								if (p > 0) {
									val = xx*yy*pow(energy_, p);
								} else {
									val = xx*yy;
								}

								coords = i*widths[0] + j*widths[1] + k*widths[2] + m*widths[3] + p*widths[4] + (totN_-totNBounds_[0])*widths[5];
								extensive_moments_.increment (coords, val);
							}
						}
					}
				}
			}
		}
	} else if (order_param_ == "N_{1}") {
		if (numSpecies[0] >= minSpecies_[0] && numSpecies[0] <= maxSpecies_[0]) {
			double val = 0.0, xx = 0.0, yy = 0.0;
			long long unsigned int coords = 0;
			std::vector < long long unsigned int > widths = extensive_moments_.getWidths_();
			for (unsigned int i = 0; i < nSpecies_; ++i) {
				for (unsigned int j = 0; j <= max_order_; ++j) {
					if (j > 0) {
						xx = pow(numSpecies[i], j);
					} else {
						xx = 1.0;
					}
					for (unsigned int k = 0; k < nSpecies_; ++k) {
						for (unsigned int m = 0; m <= max_order_; ++m) {
							if (m > 0) {
								yy = pow(numSpecies[k], m);
							} else {
								yy = 1.0;
							}
							for (unsigned int p = 0; p <= max_order_; ++p) {
								if (p > 0) {
									val = xx*yy*pow(energy_, p);
								} else {
									val = xx*yy;
								}

								coords = i*widths[0] + j*widths[1] + k*widths[2] + m*widths[3] + p*widths[4] + (numSpecies[0]-minSpecies_[0])*widths[5];
								extensive_moments_.increment (coords, val);
							}
						}
					}
				}
			}
		}
	} else {
		throw customException ("Unrecognized order parameter, cannot record extensive moments");
	}
}

/*!
 * Print the (normalized by default) extensive energy histogram for each Ntot.
 *
 * \param [in] fileName Name of the file to print to
 * \param [in] normalize Whether or not to normalize the histogram (default=true)
 * \param [in] prec Digits of precision to print for mantissa (default=30)
 */
void simSystem::printExtMoments (const std::string fileName, const bool normalize, const int prec) {
	std::ofstream of;
	std::string name = fileName+".dat";
	of.open(name.c_str(), std::ofstream::out);
	of << "# <N_i^j*N_k^m*U^p> as a function of N_tot." << std::endl;
	of << "# number_of_species: " << nSpecies_ << std::endl;
	of << "# max_order: " << max_order_ << std::endl;
	if (order_param_ == "N_{tot}") {
		of << "# species_total_upper_bound: " << totNBounds_[1] << std::endl;
		of << "# species_total_lower_bound: " << totNBounds_[0] << std::endl;
	} else if (order_param_ == "N_{1}") {
		of << "# species_1_upper_bound: " << maxSpecies_[0] << std::endl;
		of << "# species_1_lower_bound: " << minSpecies_[0] << std::endl;
	} else {
		throw customException ("Unrecognized order parameter, cannot print extensive moments");
	}
	double V = box_[0]*box_[1]*box_[2];
	of << "# volume: " << std::setprecision(prec) << V << std::endl;
	if (order_param_ == "N_{tot}") {
		of << "#\tN_tot\t";
	} else if (order_param_ == "N_{1}") {
		of << "#\tN_1\t";
	} else {
		throw customException ("Unrecognized order parameter, cannot print extensive moments");
	}
	for (unsigned int i = 0; i < nSpecies_; ++i) {
		for (unsigned int j = 0; j <= max_order_; ++j) {
			for (unsigned int k = 0; k < nSpecies_; ++k) {
				for (unsigned int m = 0; m <= max_order_; ++m) {
					for (unsigned int p = 0; p <= max_order_; ++p) {
						of << "N_"+std::to_string(i+1)+"^"+std::to_string(j)+"*N_"+std::to_string(k+1)+"^"+std::to_string(m)+"*U^"+std::to_string(p)+"\t";
					}
				}
			}
		}
	}
	of << std::endl;
	std::vector <double> h = extensive_moments_.getRawHistogram ();
	std::vector <double> ctr = extensive_moments_.getCounter ();
	std::vector <double> coords (6,0);
	long unsigned int idx = 0;
	if (normalize) {
		if (order_param_ == "N_{tot}") {
			for (unsigned int n = 0; n < totNBounds_[1]-totNBounds_[0]+1; ++n) {
				of << n+totNBounds_[0] << "\t";
				coords[5] = n;
				for (unsigned int i = 0; i < nSpecies_; ++i) {
					coords[0] = i;
					for (unsigned int j = 0; j <= max_order_; ++j) {
						coords[1] = j;
						for (unsigned int k = 0; k < nSpecies_; ++k) {
							coords[2] = k;
							for (unsigned int m = 0; m <= max_order_; ++m) {
								coords[3] = m;
								for (unsigned int p = 0; p <= max_order_; ++p) {
									coords[4] = p;
									idx = extensive_moments_.getAddress(coords);
									of << std::setprecision(prec) << h[idx]/ctr[idx] << "\t";
								}
							}
						}
					}
				}
				of << std::endl;
			}
		} else if (order_param_ == "N_{1}") {
			for (unsigned int n = 0; n < maxSpecies_[0]-minSpecies_[0]+1; ++n) {
				of << n+minSpecies_[0] << "\t";
				coords[5] = n;
				for (unsigned int i = 0; i < nSpecies_; ++i) {
					coords[0] = i;
					for (unsigned int j = 0; j <= max_order_; ++j) {
						coords[1] = j;
						for (unsigned int k = 0; k < nSpecies_; ++k) {
							coords[2] = k;
							for (unsigned int m = 0; m <= max_order_; ++m) {
								coords[3] = m;
								for (unsigned int p = 0; p <= max_order_; ++p) {
									coords[4] = p;
									idx = extensive_moments_.getAddress(coords);
									of << std::setprecision(prec) << h[idx]/ctr[idx] << "\t";
								}
							}
						}
					}
				}
				of << std::endl;
			}
		} else {
			throw customException ("Unrecognized order parameter, unable to print extensive moments");
		}
	} else {
		if (order_param_ == "N_{tot}") {
			for (unsigned int n = 0; n < totNBounds_[1]-totNBounds_[0]+1; ++n) {
				of << n+totNBounds_[0] << "\t";
				coords[5] = n;
				for (unsigned int i = 0; i < nSpecies_; ++i) {
					coords[0] = i;
					for (unsigned int j = 0; j <= max_order_; ++j) {
						coords[1] = j;
						for (unsigned int k = 0; k < nSpecies_; ++k) {
							coords[2] = k;
							for (unsigned int m = 0; m <= max_order_; ++m) {
								coords[3] = m;
								for (unsigned int p = 0; p <= max_order_; ++p) {
									coords[4] = p;
									idx = extensive_moments_.getAddress(coords);
									of << std::setprecision(prec) << h[idx] << "\t";
								}
							}
						}
					}
				}
				of << std::endl;
			}
		} else if (order_param_ == "N_{1}") {
			for (unsigned int n = 0; n < maxSpecies_[0]-minSpecies_[0]+1; ++n) {
				of << n+minSpecies_[0] << "\t";
				coords[5] = n;
				for (unsigned int i = 0; i < nSpecies_; ++i) {
					coords[0] = i;
					for (unsigned int j = 0; j <= max_order_; ++j) {
						coords[1] = j;
						for (unsigned int k = 0; k < nSpecies_; ++k) {
							coords[2] = k;
							for (unsigned int m = 0; m <= max_order_; ++m) {
								coords[3] = m;
								for (unsigned int p = 0; p <= max_order_; ++p) {
									coords[4] = p;
									idx = extensive_moments_.getAddress(coords);
									of << std::setprecision(prec) << h[idx] << "\t";
								}
							}
						}
					}
				}
				of << std::endl;
			}
		} else {
			throw customException ("Unrecognized order parameter, unable to print extensive moments");
		}
	}
	of.close();
}

/*!
 * Restart the extensive energy histogram for each Ntot from unnormalized checkpoint.
 *
 * \param [in] fileName Name of the file to load from
 * \param [in] ctr Counter for each point in the histogram
 */
void simSystem::restartExtMoments (const std::string prefix, const std::vector < double > &ctr){
	std::string fileName = prefix+".dat";

	std::ifstream infile (fileName.c_str());
	if (!infile.is_open()) {
		throw customException ("Cannot load extMoments from "+fileName);
	}

	std::string line, tmp = "";
	int lineIndex = 0, dummy;
	long long unsigned int idx;
	std::vector < double > h = extensive_moments_.getRawHistogram (), coords (6, 0);

	while(std::getline(infile,line)) {
		std::stringstream lineStream(line);
		if (lineIndex == 1) {
			std::getline(lineStream, tmp, ':');
			std::getline(lineStream, tmp, ':');
			int ns = atoi(tmp.c_str());
			if (ns != nSpecies_) {
				throw customException ("Number of speces in restart file ("+ std::to_string(ns)+") is not the same as provided in input ("+std::to_string(nSpecies_)+"), cannot restart extMom from "+fileName);
			}
		} else if (lineIndex == 2) {
			std::getline(lineStream, tmp, ':');
			std::getline(lineStream, tmp, ':');
			int mo = atoi(tmp.c_str());
			if (mo != getMaxOrder()) {
				throw customException ("Max order ("+ std::to_string(mo)+") is not the same as provided in input ("+std::to_string(getMaxOrder())+"), cannot restart extMom from "+fileName);
			}
		} if (lineIndex == 3) {
			if (order_param_ == "N_{tot}") {
				std::getline(lineStream, tmp, ':');
				std::getline(lineStream, tmp, ':');
				int high = atoi(tmp.c_str());
				if (high != totNMax()) {
					throw customException ("Max bound ("+ std::to_string(high)+") is not Nmax ("+std::to_string(totNMax())+"), cannot restart extMom from "+fileName);
				}
			} else if (order_param_ == "N_{1}") {
				std::getline(lineStream, tmp, ':');
				std::getline(lineStream, tmp, ':');
				int high = atoi(tmp.c_str());
				if (high != maxSpecies_[0]) {
					throw customException ("Max bound ("+ std::to_string(high)+") is not Nmax ("+std::to_string(maxSpecies_[0])+"), cannot restart extMom from "+fileName);
				}
			} else {
				throw customException ("Unrecognized order parameter, cannot load extensive moments");
			}
		} else if (lineIndex == 4) {
			if (order_param_ == "N_{tot}") {
				std::getline(lineStream, tmp, ':');
				std::getline(lineStream, tmp, ':');
				int low = atoi(tmp.c_str());
				if (low != totNMin()) {
					throw customException ("Min bound ("+ std::to_string(low)+") is not Nmin ("+std::to_string(totNMin())+"), cannot restart extMom from "+fileName);
				}

				// now reinstantiate the histogram
				std::vector < double > lbn (6,0), ubn(6,0);
				std::vector < long long unsigned int > nbn (6,0);
				ubn[0] = nSpecies_-1;
				ubn[1] = max_order_;
				ubn[2] = nSpecies_-1;
				ubn[3] = max_order_;
				ubn[4] = max_order_;
				ubn[5] = totNMax()-totNMin();

				nbn[0] = nSpecies_;
				nbn[1] = max_order_+1;
				nbn[2] = nSpecies_;
				nbn[3] = max_order_+1;
				nbn[4] = max_order_+1;
				nbn[5] = totNMax()-totNMin()+1;

				histogram hnn (lbn, ubn, nbn);
				extensive_moments_ = hnn;
			} else if (order_param_ == "N_{1}") {
				std::getline(lineStream, tmp, ':');
				std::getline(lineStream, tmp, ':');
				int low = atoi(tmp.c_str());
				if (low != minSpecies_[0]) {
					throw customException ("Min bound ("+ std::to_string(low)+") is not Nmin ("+std::to_string(minSpecies_[0])+"), cannot restart extMom from "+fileName);
				}

				// now reinstantiate the histogram
				std::vector < double > lbn (6,0), ubn(6,0);
				std::vector < long long unsigned int > nbn (6,0);
				ubn[0] = nSpecies_-1;
				ubn[1] = max_order_;
				ubn[2] = nSpecies_-1;
				ubn[3] = max_order_;
				ubn[4] = max_order_;
				ubn[5] = maxSpecies_[0]-minSpecies_[0];

				nbn[0] = nSpecies_;
				nbn[1] = max_order_+1;
				nbn[2] = nSpecies_;
				nbn[3] = max_order_+1;
				nbn[4] = max_order_+1;
				nbn[5] = maxSpecies_[0]-minSpecies_[0]+1;

				histogram hnn (lbn, ubn, nbn);
				extensive_moments_ = hnn;
			} else {
				throw customException ("Unrecognized order parameter, cannot load extensive moments");
			}
		} else if (lineIndex >= 7) {
			// histogram itself
			lineStream >> dummy;
			coords[5] = lineIndex-7;
			for (unsigned int i = 0; i < nSpecies_; ++i) {
				coords[0] = i;
				for (unsigned int j = 0; j <= max_order_; ++j) {
					coords[1] = j;
					for (unsigned int k = 0; k < nSpecies_; ++k) {
						coords[2] = k;
						for (unsigned int m = 0; m <= max_order_; ++m) {
							coords[3] = m;
							for (unsigned int p = 0; p <= max_order_; ++p) {
								coords[4] = p;
								idx = extensive_moments_.getAddress(coords);
								lineStream >> h[idx];
							}
						}
					}
				}
			}
		}
		lineIndex++;
	}
	infile.close();

	try {
		// this checks h and ctr same size, and by extension that h from file has same size as h in system
		extensive_moments_.set(h, ctr);
	} catch (customException &ce) {
		throw customException ("Unable to restart extMom from "+fileName+" : "+ce.what());
	}
}

/*!
 * Record the energy histogram for the system at a given Ntot.
 * Only records values when N_tot in range of [min, max].
 */
void simSystem::recordEnergyHistogram () {
	// only record if in range (removes equilibration stage to get in this range, if there was any)
	if (order_param_ == "N_{tot}") {
		if (totN_ >= totNBounds_[0] && totN_ <= totNBounds_[1]) {
			const int address = totN_-totNBounds_[0];
			energyHistogram_[address].record(energy_);
		}
	} else if (order_param_ == "N_{1}") {
		if (numSpecies[0] >= minSpecies_[0] && numSpecies[0] <= maxSpecies_[0]) {
			const int address = numSpecies[0]-minSpecies_[0];
			energyHistogram_[address].record(energy_);
		}
	} else {
		throw customException ("Unrecognized order parameter, unable to record energy histogram");
	}
}

/*!
 * Monitor the energy histogram bounds at each Ntot
 */
void simSystem::checkEnergyHistogramBounds () {
	if (order_param_ == "N_{tot}") {
		if (totN_ >= totNBounds_[0] && totN_ <= totNBounds_[1]) {
			const int address = totN_-totNBounds_[0];
			energyHistogram_lb_[address] = std::min(energyHistogram_lb_[address], energy_);
			energyHistogram_ub_[address] = std::max(energyHistogram_ub_[address], energy_);
		}
	} else if (order_param_ == "N_{1}") {
		if (numSpecies[0] >= minSpecies_[0] && numSpecies[0] <= maxSpecies_[0]) {
			const int address = numSpecies[0]-minSpecies_[0];
			energyHistogram_lb_[address] = std::min(energyHistogram_lb_[address], energy_);
			energyHistogram_ub_[address] = std::max(energyHistogram_ub_[address], energy_);
		}
	} else {
		throw customException ("Unrecognized order parameter, unable to check energy histogram bounds");
	}
}

/*!
 * Check the histogram entries and trim off zero-valued entries and bounds
 */
void simSystem::refineEnergyHistogramBounds () {
	for (std::vector < dynamic_one_dim_histogram >::iterator it = energyHistogram_.begin(); it != energyHistogram_.end(); ++it) {
		try {
			it->trim_edges();
		} catch (customException &ce) {
			std::string a = "Unable to trim edges in energyHistogram at each Ntot: ", b = ce.what();
			throw customException (a+b);
		}
	}
}

/*!
 * Re-initialize the energy histogram with internal estimates of bounds. Intended to be used at crossover stage before TMMC.
 */
void simSystem::reInitializeEnergyHistogram () {
	double lb = 0.0, ub = 0.0;
	int size = 0;
	if (energyHistogram_lb_.size() != energyHistogram_ub_.size()) {
		throw customException ("Bad energy histogram bound sizes");
	}

	if (order_param_ == "N_{tot}") {
		size = totNMax() - totNMin() + 1;
	} else if (order_param_ == "N_{1}") {
		size = maxSpecies_[0] - minSpecies_[0] + 1;
	} else {
		throw customException ("Unrecognized order parameter, unable to re-initialize energy histogram");
	}

	if (energyHistogram_lb_.size() != size) {
		throw customException ("Bad energy histogram bound sizes");
	}

	for (unsigned int i = 0; i < size; ++i) {
		if (energyHistogram_lb_[i] > energyHistogram_ub_[i]) {
			throw customException ("Bad energy histogram bound sizes");
		}
		// "Standardize" the bounds against U = 0 for to "align" the bins, already done for pkHistogram.
		// This allows overlapping windows to be merged, otherwise they are not "aligned".
		// Rounding does not account for rare edge cases where energy falls exactly on the border between bins, but does not matter since this will be automatically handled.
		// This is just to give the system a good "guess" to conserve memory.
		lb = round((energyHistogram_lb_[i] - 0.0)/energyHistDelta_)*energyHistDelta_;
		ub = round((energyHistogram_ub_[i] - 0.0)/energyHistDelta_)*energyHistDelta_;

		try {
			energyHistogram_[i].reinitialize(lb,ub,energyHistDelta_);
		} catch (customException &ce) {
			throw customException ("Unable to reinitialize the energyHistogram");
		}
	}
}

/*!
 * Print the (normalized by default) energy histogram for each Ntot. Refines bounds before each print.
 *
 * \param [in] fileName Prefix of the filename to load from
 * \param [in] normalize Whether or not to normalize the histogram (default=true)
 */
void simSystem::printEnergyHistogram (const std::string fileName, const bool normalize) {
	std::ofstream of;
	std::string name = fileName+".dat";
	of.open(name.c_str(), std::ofstream::out);
	of << "# <P(U)> as a function of " << order_param_ << "." << std::endl;
	of << "# number_of_species: " << nSpecies_ << std::endl;
	if (order_param_ == "N_{tot}") {
		of << "# species_total_upper_bound: " << totNBounds_[1] << std::endl;
		of << "# species_total_lower_bound: " << totNBounds_[0] << std::endl;
	} else if (order_param_ == "N_{1}") {
		of << "# species_1_upper_bound: " << maxSpecies_[0] << std::endl;
		of << "# species_1_lower_bound: " << minSpecies_[0] << std::endl;
	} else {
		throw customException ("Unrecognized order parameter, cannot print energy histogram");
	}
	double V = box_[0]*box_[1]*box_[2];
	of << "# volume: " << std::setprecision(30) << V << std::endl;
	of << "# Bin widths for each" << std::endl;
	for (std::vector < dynamic_one_dim_histogram >::iterator it = energyHistogram_.begin(); it != energyHistogram_.end(); ++it) {
		of << it->get_delta() << "\t";
	}
	of << std::endl;
	of << "# Bin lower bound for each" << std::endl;
	for (std::vector < dynamic_one_dim_histogram >::iterator it = energyHistogram_.begin(); it != energyHistogram_.end(); ++it) {
		of << it->get_lb() << "\t";
	}
	of << std::endl;
	of << "# Bin upper bound for each" << std::endl;
	for (std::vector < dynamic_one_dim_histogram >::iterator it = energyHistogram_.begin(); it != energyHistogram_.end(); ++it) {
		of << it->get_ub() << "\t";
	}
	of << std::endl;
	if (normalize) {
		of << "# Normalized histogram for each" << std::endl;
		for (std::vector < dynamic_one_dim_histogram >::iterator it = energyHistogram_.begin(); it != energyHistogram_.end(); ++it) {
			std::deque <double> h = it->get_hist();
			double sum = 0.0;
			for (std::deque <double>::iterator it2 = h.begin(); it2 != h.end(); ++it2) {
				sum += *it2;
			}
			for (std::deque <double>::iterator it2 = h.begin(); it2 != h.end(); ++it2) {
				of << std::setprecision(30) << *it2/sum << "\t";
			}
			of << std::endl;
		}
	} else {
		of << "# Unnormalized histogram for each" << std::endl;
		for (std::vector < dynamic_one_dim_histogram >::iterator it = energyHistogram_.begin(); it != energyHistogram_.end(); ++it) {
			std::deque <double> h = it->get_hist();
			for (std::deque <double>::iterator it2 = h.begin(); it2 != h.end(); ++it2) {
				of << std::setprecision(30) << *it2 << "\t";
			}
			of << std::endl;
		}
	}
	of.close();
}

/*!
 * Restart the energy histogram for each Ntot from unnormalized checkpoint. This will check for "alignment" of energy bins to 0.
 * So "manually" prepared files are likely to fail, but this is intended to restart from system-generated files anyway.
 *
 * \param [in] prefix Prefix of the filename to load from
 */
void simSystem::restartEnergyHistogram (const std::string prefix) {
	int minBound = 0, maxBound = 0;

	if (order_param_ == "N_{tot}") {
		maxBound = totNMax() - totNMin() + 1;
	} else if (order_param_ == "N_{1}") {
		maxBound = maxSpecies_[0] - minSpecies_[0] + 1;
	} else {
		throw customException ("Unrecognized order parameter, cannot restart enegry histogram");
	}

	std::vector < double > lb(maxBound - minBound, 0), ub(maxBound - minBound, 0), delta(maxBound - minBound, 0);
	std::string fileName = prefix+".dat";

	std::ifstream infile (fileName.c_str());
	if (!infile.is_open()) {
		throw customException ("Cannot load energyHistogram from "+fileName);
	}

	std::string line, tmp = "";
	int lineIndex = 0;
	while(std::getline(infile,line)) {
		std::stringstream lineStream(line);
		if (lineIndex == 2) {
			// get upper bound
			if (order_param_ == "N_{tot}") {
				std::getline(lineStream, tmp, ':');
				std::getline(lineStream, tmp, ':');
				int high = atoi(tmp.c_str());
				if (high != totNMax()) {
					throw customException ("Max bound ("+ std::to_string(high)+") is not Nmax("+std::to_string(totNMax())+"), cannot restart energy histogram from "+fileName);
				}
			} else if (order_param_ == "N_{1}") {
				std::getline(lineStream, tmp, ':');
				std::getline(lineStream, tmp, ':');
				int high = atoi(tmp.c_str());
				if (high != maxSpecies_[0]) {
					throw customException ("Max bound ("+ std::to_string(high)+") is not Nmax("+std::to_string(maxSpecies_[0])+"), cannot restart energy histogram from "+fileName);
				}
			} else {
				throw customException ("Unrecognized order parameter, cannot restart energy histogram");
			}
		} else if (lineIndex == 3) {
			// get lower bound
			if (order_param_ == "N_{tot}") {
				std::getline(lineStream, tmp, ':');
				std::getline(lineStream, tmp, ':');
				int low = atoi(tmp.c_str());
				if (low != totNMin()) {
					throw customException ("Min bound ("+ std::to_string(low)+") is not Nmin("+std::to_string(totNMin())+"), cannot restart energy histogram from "+fileName);
				}
			} else if (order_param_ == "N_{1}") {
				std::getline(lineStream, tmp, ':');
				std::getline(lineStream, tmp, ':');
				int low = atoi(tmp.c_str());
				if (low != minSpecies_[0]) {
					throw customException ("Min bound ("+ std::to_string(low)+") is not Nmin("+std::to_string(minSpecies_[0])+"), cannot restart energy histogram from "+fileName);
				}
			} else {
				throw customException ("Unrecognized order parameter, cannot restart energy histogram");
			}
		} else if (lineIndex == 6) {
			// delta
			for (unsigned int i = 0; i < maxBound-minBound; ++i) {
				lineStream >> delta[i];
			}
		} else if (lineIndex == 8) {
			// lower bound
			for (unsigned int i = 0; i < maxBound-minBound; ++i) {
				lineStream >> lb[i];
			}
			// check that system "aligned" the lower bounds so that a bin falls centered at U = 0
			for (unsigned int i = 0; i < maxBound-minBound; ++i) {
				const double x = (lb[i] - 0.0)/delta[i];
				const double err = fabs(round(x) - x);
				if (err >= 1.0e-6) {
					throw customException ("Energy bins not aligned to U = 0, cannot restart energy histogram from "+fileName+" (err, "+std::to_string(i)+") = "+std::to_string(err));
				}
			}
		} else if (lineIndex == 10) {
			// upper bound
			for (unsigned int i = 0; i < maxBound-minBound; ++i) {
				lineStream >> ub[i];
			}
			// now can reinitialize the histogram
			for (unsigned int i = minBound; i < maxBound; ++i) {
				try {
					energyHistogram_[i-minBound].reinitialize(lb[i-minBound], ub[i-minBound], delta[i-minBound]);
				} catch (...) {
					throw customException ("Unable to restart energy histogram from "+fileName);
				}
			}
		} else if (lineIndex >= 12) {
			// histogram itself
			std::deque <double> h = energyHistogram_[lineIndex-12].get_hist();
			for (std::deque <double>::iterator it = h.begin(); it != h.end(); ++it) {
				lineStream >> *it;
			}
			energyHistogram_[lineIndex-12].set_hist(h);
		}
		lineIndex++;
	}
	infile.close();
}

/*!
 * Record the particle number histogram for the system at a given Ntot.
 * Only records values when N_tot in range of [min, max].
 */
void simSystem::recordPkHistogram () {
	// only record if in range (removes equilibration stage to get in this range, if there was any)
	if (order_param_ == "N_{tot}") {
		if (totN_ >= totNBounds_[0] && totN_ <= totNBounds_[1]) {
			const int address = totN_-totNBounds_[0];
			for (unsigned int i = 0; i < nSpecies_; ++i) {
				pkHistogram_[i][address].record(numSpecies[i]);
			}
		}
	} else if (order_param_ == "N_{1}") {
		if (numSpecies[0] >= minSpecies_[0] && numSpecies[0] <= maxSpecies_[0]) {
			const int address = numSpecies[0]-minSpecies_[0];
			for (unsigned int i = 0; i < nSpecies_; ++i) {
				pkHistogram_[i][address].record(numSpecies[i]);
			}
		}
	} else {
		throw customException ("Unrecognized order parameter, unable to record particle histogram");
	}
}

/*!
 * Check the histogram entries and trim off zero-valued entries and bounds
 */
void simSystem::refinePkHistogramBounds () {
	for (std::vector < std::vector < dynamic_one_dim_histogram > >::iterator it = pkHistogram_.begin(); it != pkHistogram_.end(); ++it) {
		for (std::vector < dynamic_one_dim_histogram >::iterator it2 = it->begin(); it2 != it->end(); ++it2) {
			try {
				it2->trim_edges();
			} catch (customException &ce) {
				throw customException ("Unable to trim edges in pkHistogram at each Ntot");
			}
		}
	}
}

/*!
 * Print the (normalized by default) particle number histogram for each Ntot. Refines histograms before each print.
 *
 * \param [in] fileName Prefix of filename to print to
 * \param [in] normalize Whether or not to normalize the histogram (default=true)
 */
void simSystem::printPkHistogram (const std::string fileName, const bool normalize) {
	for (unsigned int i = 0; i < nSpecies_; ++i) {
		std::ofstream of;
		std::string name = fileName+"_"+std::to_string(i+1)+".dat";
		of.open(name.c_str(), std::ofstream::out);
		of << "# <P(N_" << i+1 << ")> as a function of " << order_param_ << "." << std::endl;
		of << "# number_of_species: " << nSpecies_ << std::endl;
		if (order_param_ == "N_{tot}") {
			of << "# species_total_upper_bound: " << totNBounds_[1] << std::endl;
			of << "# species_total_lower_bound: " << totNBounds_[0] << std::endl;
		} else if (order_param_ == "N_{1}") {
			of << "# species_1_upper_bound: " << maxSpecies_[0] << std::endl;
			of << "# species_1_lower_bound: " << minSpecies_[0] << std::endl;
		} else {
			throw customException ("Unrecognized order parameter, cannot print particle histogram");
		}
		double V = box_[0]*box_[1]*box_[2];
		of << "# volume: " << std::setprecision(30) << V << std::endl;
		of << "# Bin widths for each species index " << std::endl;
		for (std::vector < dynamic_one_dim_histogram >::iterator it = pkHistogram_[i].begin(); it != pkHistogram_[i].end(); ++it) {
			of << it->get_delta() << "\t";
		}
		of << std::endl;
		of << "# Bin lower bound for each species index " << std::endl;
		for (std::vector < dynamic_one_dim_histogram >::iterator it = pkHistogram_[i].begin(); it != pkHistogram_[i].end(); ++it) {
			of << it->get_lb() << "\t";
		}
		of << std::endl;
		of << "# Bin upper bound for each species index " << std::endl;
		for (std::vector < dynamic_one_dim_histogram >::iterator it = pkHistogram_[i].begin(); it != pkHistogram_[i].end(); ++it) {
			of << it->get_ub() << "\t";
		}
		of << std::endl;
		if (normalize) {
			of << "# Normalized histogram for each species index " << std::endl;
			for (std::vector < dynamic_one_dim_histogram >::iterator it = pkHistogram_[i].begin(); it != pkHistogram_[i].end(); ++it) {
				std::deque <double> h = it->get_hist();
				double sum = 0.0;
				for (std::deque <double>::iterator it2 = h.begin(); it2 != h.end(); ++it2) {
					sum += *it2;
				}
				for (std::deque <double>::iterator it2 = h.begin(); it2 != h.end(); ++it2) {
					of << std::setprecision(30) << *it2/sum << "\t";
				}
				of << std::endl;
			}
		} else {
			of << "# Unnormalized histogram for each species index " << std::endl;
			for (std::vector < dynamic_one_dim_histogram >::iterator it = pkHistogram_[i].begin(); it != pkHistogram_[i].end(); ++it) {
				std::deque <double> h = it->get_hist();
				for (std::deque <double>::iterator it2 = h.begin(); it2 != h.end(); ++it2) {
					of << std::setprecision(30) << *it2 << "\t";
				}
				of << std::endl;
			}
		}
		of.close();
	}
}

/*!
 * Restart the particle histogram for each Ntot from unnormalized checkpoint.
 *
 * \param [in] prefix Prefix of the filename to load from
 */
void simSystem::restartPkHistogram (const std::string prefix) {
	for (unsigned int spec = 0; spec < nSpecies_; ++spec) {
		int minBound = 0, maxBound = 0;

		if (order_param_ == "N_{tot}") {
			maxBound = totNMax() - totNMin() + 1;
		} else if (order_param_ == "N_{1}") {
			maxBound = maxSpecies_[0] - minSpecies_[0] + 1;
		} else {
			throw customException ("Unrecognized order parameter, cannot restart particle histogram");
		}

		std::vector < double > lb(maxBound - minBound, 0), ub(maxBound - minBound, 0), delta(maxBound - minBound, 0);
		std::string fileName = prefix+"_"+std::to_string(spec+1)+".dat";

		std::ifstream infile (fileName.c_str());
		if (!infile.is_open()) {
			throw customException ("Cannot load pkHistogram from "+fileName);
		}

		std::string line, tmp = "";
		int lineIndex = 0;
		while(std::getline(infile,line)) {
			std::stringstream lineStream(line);
			if (lineIndex == 2) {
				// get upper bound
				if (order_param_ == "N_{tot}") {
					std::getline(lineStream, tmp, ':');
					std::getline(lineStream, tmp, ':');
					int high = atoi(tmp.c_str());
					if (high != totNMax()) {
						throw customException ("Max bound ("+ std::to_string(high)+") is not Nmax ("+std::to_string(totNMax())+"), cannot restart particle histogram from "+fileName);
					}
				} else if (order_param_ == "N_{1}") {
					std::getline(lineStream, tmp, ':');
					std::getline(lineStream, tmp, ':');
					int high = atoi(tmp.c_str());
					if (high != maxSpecies_[0]) {
						throw customException ("Max bound ("+ std::to_string(high)+") is not Nmax ("+std::to_string(maxSpecies_[0])+"), cannot restart particle histogram from "+fileName);
					}
				} else {
					throw customException ("Unrecognized order parameter, cannot restart particle histogram");
				}
			} else if (lineIndex == 3) {
				// get lower bound
				if (order_param_ == "N_{tot}") {
					std::getline(lineStream, tmp, ':');
					std::getline(lineStream, tmp, ':');
					int low = atoi(tmp.c_str());
					if (low != totNMin()) {
						throw customException ("Min bound ("+ std::to_string(low)+") is not Nmin ("+std::to_string(totNMin())+"), cannot restart particle histogram from "+fileName);
					}
				} else if (order_param_ == "N_{1}") {
					std::getline(lineStream, tmp, ':');
					std::getline(lineStream, tmp, ':');
					int low = atoi(tmp.c_str());
					if (low != minSpecies_[0]) {
						throw customException ("Min bound ("+ std::to_string(low)+") is not Nmin ("+std::to_string(minSpecies_[0])+"), cannot restart particle histogram from "+fileName);
					}
				} else {
					throw customException ("Unrecognized order parameter, cannot restart particle histogram");
				}

			} else if (lineIndex == 1) {
				// check the number of species is correct
				std::getline(lineStream, tmp, ':');
				std::getline(lineStream, tmp, ':');
				int ns = atoi(tmp.c_str());
				if (ns != nSpecies_) {
					throw customException ("Number of speces in restart file ("+ std::to_string(ns)+") is not the same as provided in input ("+std::to_string(nSpecies_)+"), cannot restart particle histogram from "+fileName);
				}
			} else if (lineIndex == 6) {
				// delta
				for (unsigned int i = 0; i < maxBound-minBound; ++i) {
					lineStream >> delta[i];
				}
			} else if (lineIndex == 8) {
				// lower bound
				for (unsigned int i = 0; i < maxBound-minBound; ++i) {
					lineStream >> lb[i];
				}
			} else if (lineIndex == 10) {
				// upper bound
				for (unsigned int i = 0; i < maxBound-minBound; ++i) {
					lineStream >> ub[i];
				}
				// now can reinitialize the histogram
				for (unsigned int i = minBound; i < maxBound; ++i) {
					try {
						pkHistogram_[spec][i-minBound].reinitialize(lb[i-minBound], ub[i-minBound], delta[i-minBound]);
					} catch (...) {
						throw customException ("Unable to restart particle histogram from "+fileName);
					}
				}
			} else if (lineIndex >= 12) {
				// histogram itself
				std::deque <double> h = pkHistogram_[spec][lineIndex-12].get_hist();
				for (std::deque <double>::iterator it = h.begin(); it != h.end(); ++it) {
					lineStream >> *it;
				}
				pkHistogram_[spec][lineIndex-12].set_hist(h);
			}
			lineIndex++;
		}
		infile.close();
	}
}

/*!
 * Add a pair potential to the system which governs the pair (spec1, spec2). However, it only stores the pointer so the object must be
 * fixed in memory somewhere else throughout the simulation.
 *
 * \param [in] spec1 Species index 1 (>= 0)
 * \param [in] spec2 Species index 2 (>= 0)
 * \param [in] ppot_name Name of pair potential
 * \param [in] params Vector of parameters which define pair potential
 * \param [in] bool Optional argument of whether or not to build and maintain a cell list for this pair (spec1, spec2) (default=false)
 * \param [in] tabFile Optional argument use for tabulated potentials, this is the file to load from (default="")
 */
void simSystem::addPotential (const int spec1, const int spec2, const std::string ppot_name, const std::vector < double > &params, const bool useCellList, const std::string tabFile) {
	if (spec1 >= nSpecies_ || spec1 < 0) {
		throw customException ("Trying to define pair potential for species (1) that does not exist yet/is invalid");
	}
	if (spec2 >= nSpecies_ || spec2 < 0) {
		throw customException ("Trying to define pair potential for species (2) that does not exist yet/is invalid");
	}

	if (ppot_name == "square_well") {
		try {
			auto pp1 = std::make_shared < squareWell > ();
			pp1->setParameters(params);
			ppot[spec1][spec2] = pp1;
			auto pp2 = std::make_shared < squareWell > ();
			pp2->setParameters(params);
			ppot[spec2][spec1] = pp2;
		} catch (std::exception &ex) {
			throw customException(ex.what());
		}
	} else if (ppot_name == "lennard_jones") {
		try {
			auto pp1 = std::make_shared < lennardJones > ();
			pp1->setParameters(params);
			ppot[spec1][spec2] = pp1;
			auto pp2 = std::make_shared < lennardJones > ();
			pp2->setParameters(params);
			ppot[spec2][spec1] = pp2;
		} catch (std::exception &ex) {
			throw customException(ex.what());
		}
	} else if (ppot_name == "fs_lennard_jones") {
		try {
			auto pp1 = std::make_shared < fsLennardJones > ();
			pp1->setParameters(params);
			ppot[spec1][spec2] = pp1;
			auto pp2 = std::make_shared < fsLennardJones > ();
			pp2->setParameters(params);
			ppot[spec2][spec1] = pp2;
		} catch (std::exception &ex) {
			throw customException(ex.what());
		}
	} else if (ppot_name == "hard_sphere") {
		try {
			auto pp1 = std::make_shared < hardCore > ();
			pp1->setParameters(params);
			ppot[spec1][spec2] = pp1;
			auto pp2 = std::make_shared < hardCore > ();
			pp2->setParameters(params);
			ppot[spec2][spec1] = pp2;
		} catch (std::exception &ex) {
			throw customException(ex.what());
		}
	} else if (ppot_name == "tabulated") {
		try {
			auto pp1 = std::make_shared < tabulated > ();
			pp1->setParameters(params);
			pp1->loadPotential(tabFile);
			ppot[spec1][spec2] = pp1;
			auto pp2 = std::make_shared < tabulated > ();
			pp2->setParameters(params);
			pp2->loadPotential(tabFile);
			ppot[spec2][spec1] = pp2;
		} catch (std::exception &ex) {
			throw customException(ex.what());
		}
	} else {
		throw customException("Unrecognized pair potential name for species "+numToStr(spec1)+", "+numToStr(spec2));
	}

	ppotSet_[spec1][spec2] = true;
	ppotSet_[spec2][spec1] = true;

	if (useCellList) {
		sendMsg("Setting up cell list for interactions between type "+numToStr(spec1)+" and "+numToStr(spec2));
		// Add creation of cell lists
		if ((ppot[spec1][spec2]->rcut() > box_[0]/3.0) || (ppot[spec1][spec2]->rcut() > box_[1]/3.0) || (ppot[spec1][spec2] ->rcut() > box_[2]/3.0)) {
			sendErr("Cutoff ("+numToStr(ppot[spec1][spec2]->rcut())+") larger than 1.0/3.0 boxsize, disabling cell lists for this interaction");
			useCellList_[spec1][spec2] = false;
			useCellList_[spec2][spec1] = false;
		} else {
			sendMsg("Creating Cell list with r_cut = "+numToStr(ppot[spec1][spec2]->rcut()));
			useCellList_[spec1][spec2] = true;
			useCellList_[spec2][spec1] = true;

			std::vector <atom*> dummyList(0);

			if (cellListsByPairType_[spec1][spec2] == NULL) {
				cellLists_.push_back(cellList(box_, ppot[spec1][spec2]->rcut(), dummyList));
				cellListsByPairType_[spec1][spec2] = &cellLists_[cellLists_.size()-1];
			}
			if (cellListsByPairType_[spec2][spec1] == NULL) {
				cellLists_.push_back(cellList(box_, ppot[spec2][spec1]->rcut(), dummyList));
				cellListsByPairType_[spec2][spec1] = &cellLists_[cellLists_.size()-1];
			}
		}
	} else {
		useCellList_[spec1][spec2] = false;
		useCellList_[spec2][spec1] = false;
	}
}

/*!
 * Print an XYZ file of the instantaneous system configuration.  This can be read in at a later time via estart() function.
 *
 * \param [in] filename File to store XYZ coordinates to
 * \param [in] comment Comment line for the file
 * \param [in] overwrite Flag to overwrite file if it already exists or to append (default = true, overwrite)
 */
void simSystem::printSnapshot (std::string filename, std::string comment, bool overwrite) {
	if (overwrite) {
		std::ofstream outfile (filename.c_str(), std::ofstream::trunc);
		int tot = 0;
	    for (unsigned int j = 0; j < nSpecies_; ++j) {
	        tot += numSpecies[j]; // only count fully inserted species
	    }

	    outfile << tot << std::endl;
	    outfile << comment << std::endl;

	    for (unsigned int j = 0; j < nSpecies_; ++j) {
	    	long long int num = numSpecies[j];
			if (Mcurrent_ > 1 && fractionalAtomType_ == j) {
	    		num += 1; // account for partially inserted atom
			}
			for (unsigned int i = 0; i < num; ++i) {
				if (atoms[j][i].mState == 0) { // only print fully inserted atoms
	        		outfile << j << "\t" <<  std::setprecision(15) << atoms[j][i].pos[0] << "\t" << std::setprecision(15) << atoms[j][i].pos[1] << "\t" << std::setprecision(15) << atoms[j][i].pos[2] << std::endl;
	    		}
			}
		}

	    outfile.close();
	} else {
		std::ofstream outfile (filename.c_str(), std::ofstream::out | std::ofstream::app);
		int tot = 0;
	    for (unsigned int j = 0; j < nSpecies_; ++j) {
	        tot += numSpecies[j]; // only count fully inserted species
	    }

	    outfile << tot << std::endl;
	    outfile << comment << std::endl;

	    for (unsigned int j = 0; j < nSpecies_; ++j) {
	    	long long int num = numSpecies[j];
			if (Mcurrent_ > 1 && fractionalAtomType_ == j) {
	    		num += 1; // account for partially inserted atom
			}
			for (unsigned int i = 0; i < num; ++i) {
				if (atoms[j][i].mState == 0) { // only print fully inserted atoms
	        		outfile << j << "\t" <<  std::setprecision(15) << atoms[j][i].pos[0] << "\t" << std::setprecision(15) << atoms[j][i].pos[1] << "\t" << std::setprecision(15) << atoms[j][i].pos[2] << std::endl;
	    		}
			}
		}

	    outfile.close();
	}
}

/*!
 * Read an XYZ file as the system's initial configuration.  Note that the number of species, etc. must already be specified in the constructor.
 * Will also reset and calculate the energy from scratch so these potentials should be set before reading in a restart file.
 *
 * \param [in] filename File to read XYZ coordinates from
 */
void simSystem::readConfig (std::string filename) {
	sendMsg("Reading initial configuration from "+filename);

	std::ifstream infile (filename.c_str());
	if (!infile.is_open()) {
		sendErr("Cannot open "+filename);
		exit(SYS_FAILURE);
	}

	std::string line;
	std::vector < atom > sysatoms;
	std::vector < int > index;
	int natoms = 0;
	int lineIndex = 0;
	while(std::getline(infile,line)) {
		std::stringstream lineStream(line);
		if (lineIndex == 0) {
			lineStream >> natoms;
			index.resize(natoms);
			sysatoms.resize(natoms);
		} else if (lineIndex > 1) {
			lineStream >> index[lineIndex-2] >> sysatoms[lineIndex-2].pos[0] >> sysatoms[lineIndex-2].pos[1] >> sysatoms[lineIndex-2].pos[2];
		}
		lineIndex++;
	}
	infile.close();

	// sort by type
	std::map < int, int > types;
	for (unsigned int j = 0; j < natoms; ++j) {
		if (types.find(index[j]) != types.end()) {
			types[index[j]] += 1;
		} else {
			types[index[j]] = 1;
		}
	}

	int maxType = -1;
	for (std::map < int, int >::iterator it = types.begin(); it != types.end(); ++it) {
		maxType = std::max(maxType, it->first);
		if (it->first < 0 || it->first >= nSpecies_) {
			throw customException ("Restart file corrupted, types out of range");
		}
	}

	// check if within global bounds
	if (order_param_ == "N_{tot}") {
		if (sysatoms.size() > totNBounds_[1] || sysatoms.size() < totNBounds_[0]) {
			throw customException ("Number of particles ("+std::to_string(sysatoms.size())+") in the restart file out of target range ["+std::to_string(totNBounds_[0])+", "+std::to_string(totNBounds_[1])+"]");
		}
	} else if (order_param_ == "N_{1}") {
		for (unsigned int i = 0; i < nSpecies_; ++i) {
			if (types[i] < minSpecies_[i] || types[i] > maxSpecies_[i]) {
				throw customException ("Number of species "+std::to_string(i+1)+" ("+std::to_string(types[i])+") in the restart file out of target range ["+std::to_string(minSpecies_[i])+", "+std::to_string(maxSpecies_[i])+"]");
			}
		}
	} else {
		throw customException ("Unrecognized order parameter, cannot read configuration from file");
	}

	// check that pair potentials exist so energy can be calculated
	for (unsigned int i = 0; i < nSpecies_; ++i) {
		for (unsigned int j = 0; j < nSpecies_; ++j) {
			if (!potentialIsSet(i, j)) {
				throw customException("Not all pair potentials are set, so cannot initial from file");
			}
		}
	}

	// empty out the system before adding new atoms in - all atoms "fully inserted" so no partial ones to worry about
	if (Mcurrent_ != 0) {
		throw customException ("System cannot be restarted from "+filename+", for some reason current expanded state != 0");
	}
	for (unsigned int i = 0; i < nSpecies_; ++i) {
		const int ns = numSpecies[i];
		for (int j = ns-1; j >=0; --j) {
			for (int k = 0; k < Mtot_; ++k) {
				deleteAtom (i, j, true);
			}
		}
	}
	if (totN_ != 0) {
		throw customException ("total N = "+std::to_string(totN_)+" != 0 after system supposedly emptied");
	}
	for (unsigned int i = 0; i < nSpecies_; ++i) {
		if (numSpecies[i] != 0) {
			throw customException ("Number of molecules of species #"+std::to_string(i+1)+" = "+std::to_string(numSpecies[i])+" != 0 after system supposedly emptied");
		}
	}
	if (Mcurrent_ != 0) {
		throw customException ("M state != 0 after system supposedly emptied");
	}

	energy_ = 0.0;
	for (unsigned int j = 0; j < sysatoms.size(); ++j) {
		try {
			// "partially" insert each atom so it goes through all the stages
			insertAtom (index[j], &sysatoms[j]);
			for (unsigned int k = 1; k < Mtot_; ++k) {
				insertAtom (index[j], fractionalAtom_); // this will check that within each species own max and min, global bounds handled above
			}
		} catch (customException &ce) {
			std::string a = "Could not initialize system from restart file, ", b = ce.what();
			throw customException (a+b);
		}
	}

	// recalculate system's initial energy
	energy_ = scratchEnergy();
	sendMsg("Successfully loaded initial configuration from "+filename);
}

/*!
 * Return the list of neighbors of type A, around a particle of type B which is passed
 *
 * \param [in] typeIndexA Index of first atom type
 * \param [in] typeIndexB Index of second atom type
 * \param [in] atom Pointer to atom to find neighbors around
 *
 * \return neighbor_list
 */
std::vector < atom* > simSystem::getNeighborAtoms(const unsigned int typeIndexA, const unsigned int typeIndexB, atom* _atom) {
	std::vector < atom* > neighbors;

	int end = numSpecies[typeIndexA];
	if (Mcurrent_ > 0 && typeIndexA == fractionalAtomType_) {
		// account for partial atom too
		end++;
	}
	neighbors.reserve(end);

	// if no cell lists are defined for this interaction, return all particles
	if (!useCellList_[typeIndexA][typeIndexB]) {
		for (unsigned int i = 0; i < end; ++i) {
			if (_atom != &atoms[typeIndexA][i]) { // watch out for self in case typeA = typeB
				neighbors.push_back(&atoms[typeIndexA][i]);
			}
		}
	} else if (useCellList_[typeIndexA][typeIndexB]) {
		cellList* cl = cellListsByPairType_[typeIndexA][typeIndexB];
		const unsigned int cellIndex = cl->calcIndex(_atom->pos[0], _atom->pos[1], _atom->pos[2]);

		// loop over own cell
		for (unsigned int i = 0; i < cl->cells[cellIndex].size(); ++i) {
			if (_atom != cl->cells[cellIndex][i]) {
				neighbors.push_back(cl->cells[cellIndex][i]);
			}
		}

		// loop over neighboring cells
		for (unsigned int i = 0; i < cl->neighbours[cellIndex].size(); ++i) {
			const unsigned int neighborCellIndex = cl->neighbours[cellIndex][i];
			for (unsigned int j = 0; j < cl->cells[neighborCellIndex].size(); ++j) {
				if (_atom != cl->cells[neighborCellIndex][j]) {
					neighbors.push_back(cl->cells[neighborCellIndex][j]);
				}
			}
		}
	}

	return neighbors;
}

/*!
 * Recalculate the energy of the system from scratch.
 *
 * \returns totU Total energy of the system
 */
const double simSystem::scratchEnergy () {
   	double totU = 0.0;
   	double V = 1.0;

	for (unsigned int i = 0; i < box_.size(); ++i) {
    	V *= box_[i];
    }

    for (unsigned int spec1 = 0; spec1 < nSpecies_; ++spec1) {
        int num1 = 0, adj1 = 0;
        try {
    		num1 = numSpecies[spec1];
    	} catch (customException &ce) {
    		std::string a = "Cannot recalculate energy from scratch: ", b = ce.what();
    		throw customException (a+b);
		}

		// Possibly have fractionally inserted atom
		if (fractionalAtomType_ == spec1 && Mcurrent_ > 0) {
			adj1 = 1;
		}

		// Wall/barrier interactions
		for (unsigned int j = 0; j < num1+adj1; ++j) {
			double dU = 0.0;
			try {
				dU = speciesBarriers[spec1].energy(&atoms[spec1][j], box_);
			} catch (customException &ce) {
				std::string a = "Cannot recalculate energy from scratch: ", b = ce.what();
				throw customException (a+b);
			}
			if (dU == NUM_INFINITY) {
				return NUM_INFINITY;
			} else {
				totU += dU;
			}
		}

        // Interactions with same type
        for (unsigned int j = 0; j < num1+adj1; ++j) {
			double dU = 0.0;
	        for (unsigned int k = j+1; k < num1+adj1; ++k) {
       			try {
                    dU = ppot[spec1][spec1]->energy(&atoms[spec1][j], &atoms[spec1][k], box_);
                } catch (customException &ce) {
                	std::string a = "Cannot recalculate energy from scratch: ", b = ce.what();
                    throw customException (a+b);
                }
				if (dU < NUM_INFINITY) {
					totU += dU;
				} else {
					return NUM_INFINITY;
				}
            }
        }

        // Add tail correction to potential energy but only for atoms fully inserted
#ifdef FLUID_PHASE_SIMULATIONS
        if ((ppot[spec1][spec1]->useTailCorrection) && (num1 > 1)) {
        	totU += (num1)*0.5*ppot[spec1][spec1]->tailCorrection((num1-1)/V); // This is never infinite
        }
#endif

        // Interactions with other unique types
        for (unsigned int spec2 = spec1+1; spec2 < nSpecies_; ++spec2) {
            int num2 = 0, adj2 = 0;
			double dU = 0.0;

        	try {
                num2 = numSpecies[spec2];
            } catch (customException &ce) {
            	std::string a = "Cannot recalculate energy from scratch: ", b = ce.what();
        		throw customException (a+b);
    		}

			if (fractionalAtomType_ == spec2 && Mcurrent_ > 0) {
				adj2 = 1;
			}

            for (unsigned int j = 0; j < num1+adj1; ++j) {
                for (unsigned int k = 0; k < num2+adj2; ++k) {
                    try {
                        dU = ppot[spec1][spec2]->energy(&atoms[spec1][j], &atoms[spec2][k], box_);
					} catch (customException &ce) {
                        std::string a = "Cannot recalculate energy from scratch: ", b = ce.what();
                        throw customException (a+b);
                    }
					if (dU < NUM_INFINITY) {
						totU += dU;
					} else {
						return NUM_INFINITY;
					}
                }
            }

        	// Add tail correction to potential energy but only bewteen fully inserted species
#ifdef FLUID_PHASE_SIMULATIONS
            if ((ppot[spec1][spec2]->useTailCorrection) && (num2 > 0) && (num1 > 0)) {
                totU += (num1)*ppot[spec1][spec2]->tailCorrection(num2/V); // Never infinite
            }
#endif
        }
    }

    if (toggleKE_ == true) {
    	double ns = 0.0;
    	for (unsigned int i = 0; i < nSpecies_; ++i) {
    		ns += numSpecies[i];
    	}
    	totU += 1.5/beta_*ns; // Only adjust for FULLY-INSERTED ATOMS
    }

    return totU;
}

/*!
 * Returns the absolute maximum number of a given species type allowed in the system.
 *
 * \param [in] index  Species index to query
 *
 * \return maxSpecies Maximum number of them allowed
 */
const int simSystem::maxSpecies (const int index) {
	if (maxSpecies_.begin() == maxSpecies_.end()) {
        throw customException ("No species in the system, cannot report a maximum");
    }
    if (maxSpecies_.size() <= index) {
    	throw customException ("System does not contain that species, cannot report a maximum");
    } else  {
    	return maxSpecies_[index];
    }
}

/*!
 * Returns the absolute minimum number of a given species type allowed in the system.
 *
 * \param [in] index  Species index to query
 *
 * \return minSpecies Minimum number of them allowed
 */
const int simSystem::minSpecies (const int index) {
	if (minSpecies_.begin() == minSpecies_.end()) {
        	throw customException ("No species in the system, cannot report a minimum");
    	}
    	if (minSpecies_.size() <= index) {
        	throw customException ("System does not contain that species, cannot report a minimum");
    	} else  {
        	return minSpecies_[index];
    	}
}

/*!
 * Return a pointer to the TMMC biasing object, if using TMMC, else throws an exception.
 *
 * \return tmmc Pointer to TMMC biasing object being used.
 */
tmmc* simSystem::getTMMCBias () {
	if (useTMMC == true) {
		return tmmcBias;
	} else {
		throw customException ("Not using TMMC");
	}
}

/*!
 * Return a pointer to the TMMC biasing object, if using TMMC, else throws an exception.
 *
 * \return wala Pointer to WALA biasing object being used.
 */
wala* simSystem::getWALABias () {
	if (useWALA == true) {
		return wlBias;
	} else {
		throw customException ("Not using WALA");
	}
}

/*
 * Start using a Wang-Landau bias in the simulation. Throws an exception if input values are illegal or there is another problem (e.g. memory).
 *
 * \param [in] lnF Factor by which the estimate of the density of states in updated each time it is visited.
 * \param [in] g Factor by which lnF is reduced (multiplied) once "flatness" has been achieved.
 * \param [in] s Factor by which the min(H) must be within the mean of H to be considered "flat", e.g. 0.8 --> min is within 20% error of mean
 * \param [in] Mtot Total number of expanded ensemble state allowed within the system
 */
void simSystem::startWALA (const double lnF, const double g, const double s, const int Mtot) {
	// initialize the wala object
	if (order_param_ == "N_{tot}") {
		try {
			wlBias = new wala (lnF, g, s, totNBounds_[1], totNBounds_[0], Mtot, box_);
		} catch (customException& ce) {
			throw customException ("Cannot start Wang-Landau biasing in system: "+std::to_string(*ce.what()));
		}
	} else if (order_param_ == "N_{1}") {
		try {
			wlBias = new wala (lnF, g, s, maxSpecies_[0], minSpecies_[0], Mtot, box_);
		} catch (customException& ce) {
			throw customException ("Cannot start Wang-Landau biasing in system: "+std::to_string(*ce.what()));
		}
	} else {
		throw customException ("Unrecognized order parameter, unable to start Wang-Landau");
	}

	useWALA = true;
}

/*!
 * Start using a transition-matrix in the simulation. Throws an exception if input values are illegal or there is another problem (e.g. memory).
 *
 * \param [in] tmmcSweepSize Number of times each transition in the collection matrix must be visited for a "sweep" to be completed
 * \param [in] Mtot Total number of expanded ensemble state allowed within the system
 */
void simSystem::startTMMC (const long long int tmmcSweepSize, const int Mtot) {
	// initialize the tmmc object
	if (order_param_ == "N_{tot}") {
		try {
			tmmcBias = new tmmc (totNBounds_[1], totNBounds_[0], Mtot, tmmcSweepSize, box_);
		} catch (customException& ce) {
			throw customException ("Cannot start TMMC biasing in system: "+std::to_string(*ce.what()));
		}
	} else if (order_param_ == "N_{1}") {
		try {
			tmmcBias = new tmmc (maxSpecies_[0], minSpecies_[0], Mtot, tmmcSweepSize, box_);
		} catch (customException& ce) {
			throw customException ("Cannot start TMMC biasing in system: "+std::to_string(*ce.what()));
		}
	} else {
		throw customException ("Unrecognized order parameter, unable to start TMMC");
	}

	useTMMC = true;
}

/*!
 * Calculate the bias based on a systems current state and the next state being proposed.
 *
 * \param [in] sys System object containing the current state of the system.
 * \param [in] opFinal Final value of order parameter in proposed state (e.g., N_tot, or N_1 after a move would be accepted).
 * \param [in] mFinal Final value of the expanded ensemble state of the system.
 * \param [in] p_u Ratio of the system's partition in the final and initial state (e.g. unbiased p_acc = min(1, p_u)).
 *
 * \return rel_bias The value of the relative bias to apply in the metropolis criteria during sampling
 */
const double calculateBias (simSystem &sys, const int opFinal, const int mFinal) {
	double rel_bias = 1.0;

	if (sys.useTMMC && !sys.useWALA) {
		// TMMC biasing
		if (sys.getOP() == "N_{tot}") {
			const long long int address1 = sys.tmmcBias->getAddress(sys.getTotN(), sys.getCurrentM()), address2 = sys.tmmcBias->getAddress(opFinal, mFinal);
			const double b1 = sys.tmmcBias->getBias (address1), b2 = sys.tmmcBias->getBias (address2);
			rel_bias = exp(b2-b1);
		} else if (sys.getOP() == "N_{1}") {
			const long long int address1 = sys.tmmcBias->getAddress(sys.numSpecies[0], sys.getCurrentM()), address2 = sys.tmmcBias->getAddress(opFinal, mFinal);
			const double b1 = sys.tmmcBias->getBias (address1), b2 = sys.tmmcBias->getBias (address2);
			rel_bias = exp(b2-b1);
		} else {
			throw customException ("Unrecognized order parameter, cannot compute bias");
		}
    } else if (!sys.useTMMC && sys.useWALA) {
    	// Wang-Landau Biasing
		if (sys.getOP() == "N_{tot}") {
			const long long int address1 = sys.wlBias->getAddress(sys.getTotN(), sys.getCurrentM()), address2 = sys.wlBias->getAddress(opFinal, mFinal);
	    	const double b1 = sys.wlBias->getBias (address1), b2 = sys.wlBias->getBias (address2);
	    	rel_bias = exp(b2-b1);
		} else if (sys.getOP() == "N_{1}") {
			const long long int address1 = sys.wlBias->getAddress(sys.numSpecies[0], sys.getCurrentM()), address2 = sys.wlBias->getAddress(opFinal, mFinal);
	    	const double b1 = sys.wlBias->getBias (address1), b2 = sys.wlBias->getBias (address2);
	    	rel_bias = exp(b2-b1);
		} else {
			throw customException ("Unrecognized order parameter, cannot compute bias");
		}
    } else if (sys.useTMMC && sys.useWALA) {
    	// Crossover phase where we use WL but update TMMC collection matrix
		if (sys.getOP() == "N_{tot}") {
			const int address1 = sys.wlBias->getAddress(sys.getTotN(), sys.getCurrentM()), address2 = sys.wlBias->getAddress(opFinal, mFinal);
	    	const double b1 = sys.wlBias->getBias (address1), b2 = sys.wlBias->getBias (address2);
	    	rel_bias = exp(b2-b1);
		} else if (sys.getOP() == "N_{1}") {
			const int address1 = sys.wlBias->getAddress(sys.numSpecies[0], sys.getCurrentM()), address2 = sys.wlBias->getAddress(opFinal, mFinal);
	    	const double b1 = sys.wlBias->getBias (address1), b2 = sys.wlBias->getBias (address2);
	    	rel_bias = exp(b2-b1);
		} else {
			throw customException ("Unrecognized order parameter, cannot compute bias");
		}
    } else {
    	// No biasing
    	rel_bias = 1.0;
    }

	return rel_bias;
}
