#include "mover.h"

/*!
 * Instantiate moves class.
 *
 * \param in M Number of expanded ensemble stages for insert/delete moves
 */
moves::moves (const int M) {
	try {
		setM (M);
	} catch (customException &ce) {
		sendErr(ce.what());
		exit(SYS_FAILURE);
	}
}

/*!
 * Destructor for moves class.
 */
moves::~moves () {
	;
}

/*!
 * Clear all moves and related information from the class, but leaves M intact.
 */
void moves::clearAll () {
	normProbabilities_.clear();
	rawProbabilities_.clear();
	succeeded_.clear();
	attempted_.clear();
	ownedMoves_.clear();
}

/*!
 * Set the value of M.
 *
 * \param in M Number of expanded ensemble stages for insert/delete moves
 */
void moves::setM (const int M) {
	if (M > 0) {
        M_ = M;
	} else {
        throw customException ("Error, number of expanded ensemble stages must be > 0");
	}
}

/*!
 * Print move information to file.  Appends by default.
 *
 * \param [in] filename Name of file to print to.
 */
void moves::print (const std::string filename) {
	std::ofstream statFile (filename.c_str(), std::ofstream::out | std::ofstream::app);
    std::vector < std::vector < double > > stats = reportMoveStatistics();
	statFile << "Time: " << getTimeStamp() << std::endl;
    statFile << "---------- Move Statistics --------- " << std::endl << "Move\t\% Success" << std::endl;
    for (unsigned int i = 0; i < stats.size(); ++i) {
        double prod = 1.0;
        for (unsigned int j = 0; j < stats[i].size(); ++j) {
            prod *= stats[i][j];
            statFile << ownedMoves_[i]->myName() << " (from M = " << j << ")\t" << stats[i][j]*100.0 << std::endl;
        }
        if (stats[i].size() > 1) {
            statFile << "-------------------------------------\nProduct of percentages (%) = " << prod*100 << "\n-------------------------------------" << std::endl;
        }
    }
	statFile << "------------------------------------ " << std::endl;
    statFile.close();
}

/*!
 * Add insertion move.
 *
 * \param [in] index Particle index to operate on
 * \param [in] prob Probability
 */
void moves::addInsert (const int index, const double prob) {
	auto om = std::make_shared < insertParticle > (index, "insert");
	ownedMoves_.push_back(om);
	try {
		addOn_(ownedMoves_.back()->changeN(), prob);
	} catch (std::exception &ex) {
		const std::string msg = ex.what();
		throw customException ("Cannot add insertion move for species "+numToStr(index+1)+" : "+msg);
	}
}

/*!
 * Add deletion move.
 *
 * \param [in] index Particle index to operate on
 * \param [in] prob Probability
 */
void moves::addDelete (const int index, const double prob) {
	auto om = std::make_shared < deleteParticle > (index, "delete");
	ownedMoves_.push_back(om);
	try {
		addOn_(ownedMoves_.back()->changeN(), prob);
	} catch (std::exception &ex) {
		const std::string msg = ex.what();
		throw customException ("Cannot add deletion move for species "+numToStr(index+1)+" : "+msg);
	}
}

/*!
 * Add swap move.
 *
 * \param [in] index1 Particle index 1 to operate on
 * \param [in] index2 Particle index 2 to operate on
 * \param [in] prob Probability
 */
void moves::addSwap (const int index1, const int index2, const double prob) {
	auto om = std::make_shared < swapParticles > (index1, index2, "swap");
	ownedMoves_.push_back(om);
	try {
		addOn_(ownedMoves_.back()->changeN(), prob);
	} catch (std::exception &ex) {
		const std::string msg = ex.what();
		throw customException ("Cannot add swap move for species pair ("+numToStr(index1+1)+","+numToStr(index2+1)+") : "+msg);
	}
}

/*!
 * Add translate move.
 *
 * \param [in] index Particle index to operate on
 * \param [in] prob Probability
 * \param [in] maxD Maximium translation
 * \param [in] box Box dimensions
 */
void moves::addTranslate (const int index, const double prob, const double maxD, const std::vector < double > &box) {
	auto om = std::make_shared < translateParticle > (index, "translate");
	try {
		om->setMaxTranslation (maxD, box);
		ownedMoves_.push_back(om);
		addOn_(ownedMoves_.back()->changeN(), prob);
	} catch (std::exception &ex) {
		const std::string msg = ex.what();
		throw customException ("Cannot add translation move for species "+numToStr(index+1)+" : "+msg);
	}
}

void moves::addOn_ (bool changeN, const double probability) {
	if (probability < 0.0) {
		throw customException ("Probability/weight cannot be less than 0");
	}

	// add new move to the class
	rawProbabilities_.push_back(probability);
	normProbabilities_.resize(rawProbabilities_.size());
    int size = 0;
    if (changeN) {
      	size = M_;
    } else {
        size = 1;
    }
    if (succeeded_.end() == succeeded_.begin()) {
    	succeeded_.resize(1);
    	attempted_.resize(1);
    	succeeded_[0].resize(size, 0.0);
    	attempted_[0].resize(size, 0.0);
    } else {
    	succeeded_.resize(succeeded_.size()+1);
    	attempted_.resize(attempted_.size()+1);
    	succeeded_[succeeded_.size()-1].resize(size, 0.0);
    	attempted_[attempted_.size()-1].resize(size, 0.0);
    }

	// update move probabilities
	double sum = 0.0;
	for (unsigned int i = 0; i < rawProbabilities_.size(); ++i) {
		sum += rawProbabilities_[i];
	}

	normProbabilities_[0] = rawProbabilities_[0]/sum;
	for (unsigned int i = 1; i < rawProbabilities_.size(); ++i) {
		normProbabilities_[i] = rawProbabilities_[i]/sum + normProbabilities_[i-1];
	}

	// for exactness, specify the upper bound
	normProbabilities_[normProbabilities_.size()-1] = 1.0;
}

/*!
 * Choose a move to make. If in an expanded ensemble, will restrict moves which change the number of particles to the atom type
 * that is currently on partially in the system.
 *
 * \param [in] sys simSystem object to make a move in.
 */
void moves::makeMove (simSystem &sys) {
	if (sys.getTotalM() != M_) {
        throw customException ("Error, M in system different from M in moves class operating on the system");
    }
	int moveChosen = -1, succ = 0, mIndex = 0;
	bool done = false;
	while (!done) {
		const double ran = rng (&RNG_SEED);
		for (unsigned int i = 0; i < normProbabilities_.size(); ++i) {
			if (ran < normProbabilities_[i]) {
				if (sys.getTotalM() > 1) {
					// expanded ensemble has to check the moves because have to only work on the partially inserted atom
					if ((ownedMoves_[i]->changeN() == true) && (ownedMoves_[i]->whatType() != sys.getFractionalAtomType()) && (sys.getCurrentM() > 0)) {
						// reject this choice because we must only insert/delete the type that is already partially inserted IFF we are *already* in a partially inserted state
						// choose a new move
						done = false;
						break;
					} else {
                        // get M before move happens which can change the state of the system
                        if (ownedMoves_[i]->changeN()) {
                            mIndex = sys.getCurrentM();
                        }
						try {
							succ = ownedMoves_[i]->make(sys);
						} catch (customException &ce) {
							std::string a = "Failed to make a move properly: ";
							std::string b = ce.what();
							throw customException(a+b);
						}
						done = true;
			    		moveChosen = i;
			    		break;
					}
				} else {
					// without expanded ensemble, inserts/deletes can proceed unchecked
					try {
						succ = ownedMoves_[i]->make(sys);
					} catch (customException &ce) {
						std::string a = "Failed to make a move properly: ";
						std::string b = ce.what();
						throw customException(a+b);
					}
					done = true;
                    moveChosen = i;
                    mIndex = 0;
                    break;
				}
			}
		}
	}

	if (moveChosen < 0) {
		throw customException("Failed to choose a move properly");
	}

	attempted_[moveChosen][mIndex] += 1.0;
	succeeded_[moveChosen][mIndex] += succ;
}

/*!
 * Report the statistics on the success/failure of each move made so far. If the move changes total number
 * of particles in the system, there is a column for each expanded state it traverses.
 *
 * \return ans Number of Success / Total Attempts for each move
 */
std::vector < std::vector < double > > moves::reportMoveStatistics () {
	std::vector < std::vector < double > > ans = succeeded_;
	if (attempted_.begin() == attempted_.end()) {
		throw customException ("No moves added to system");
    }
    for (unsigned int i = 0; i < attempted_.size(); ++i) {
    	for (unsigned int j = 0; j < attempted_[i].size(); ++j) {
        	ans[i][j] /= attempted_[i][j];
    	}
    }
    return ans;
}
