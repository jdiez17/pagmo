/*****************************************************************************
 *   Copyright (C) 2008, 2009 Advanced Concepts Team (European Space Agency) *
 *   act@esa.int                                                             *
 *                                                                           *
 *   This program is free software; you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation; either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program; if not, write to the                           *
 *   Free Software Foundation, Inc.,                                         *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.               *
 *****************************************************************************/

// 16/05/08 Created by Dario Izzo.

#include <cmath>
#include <functional>
#include <typeinfo>

#include "../../exceptions.h"
#include "GOproblem.h"
#include "population.h"
#include "rng.h"

	Population::Population(const GOProblem &p):m_problem(p.clone()) {}

	Population::Population(const GOProblem &p, int N):m_problem(p.clone())
	{
		createRandomPopulation(N);
	}

	Population::Population(const Population &p):pop(p.pop),m_problem(p.m_problem->clone()) {}

	Population &Population::operator=(const Population &p)
	{
		if (typeid(*m_problem) != typeid(*p.m_problem)) {
			pagmo_throw(type_error, "problem types are not comaptible in population assignment");
		}
		pop = p.pop;
		m_problem.reset(p.m_problem->clone());
		return *this;
	}

	void Population::createRandomPopulation(int N)
	{
		pop.clear();
		for (int i=0; i < N; i++){
			pop.push_back(Individual(*m_problem));
		}
	};

	void Population::push_back(const Individual &x)
	{
		if (x.getDecisionVector().size() != m_problem->getLB().size()) {
			pagmo_throw(value_error,"cannot insert individual into population, size mismatch");
		}
		pop.push_back(x);
	};

	size_t Population::size() const
	{
		return pop.size();
	};

	const Individual &Population::extractBestIndividual() const
	{
		return extract_most<std::less<double> >();
	}

	const Individual &Population::extractWorstIndividual() const
	{
		return extract_most<std::greater<double> >();
	}

	Population Population::extractRandomDeme(int N, std::vector<size_t> &picks)
	{
		if (N > (int)size()) {
			pagmo_throw(index_error,"cannot extract deme whose size is greater than the original population");
		}
		// Empty picks first.
		picks.clear();
		static_rng_double drng;
		Population deme(*m_problem,0);
		const size_t pop_size = size();
		std::vector<size_t> PossiblePicks;
		PossiblePicks.reserve(pop_size);
		for (size_t i = 0; i < pop_size; ++i) {
			PossiblePicks.push_back(i);
		}
		for (int i = 0; i < N; ++i){
			//we pick a random position between 0 and popsize-1
			const size_t Pick = (size_t)(drng() * PossiblePicks.size());
			//and store it
			picks.push_back(PossiblePicks[Pick]);
			//we insert the corresponding individual in the deme
			deme.push_back(pop[PossiblePicks[Pick]]);
			//and erase it from the possible picks
			PossiblePicks.erase(PossiblePicks.begin() + Pick);
		}
		return deme;
	};

	void Population::insertDeme(const Population &deme, const std::vector<size_t> &picks)
	{
		ll_insert_deme<false>(deme,picks);
	}

	void Population::insertDemeForced(const Population &deme, const std::vector<size_t> &picks)
	{
		ll_insert_deme<true>(deme,picks);
	}

	void Population::insertBestInDeme(const Population &deme, const std::vector<size_t> &picks)
	{
		const size_t Ndeme = deme.size(), pop_size = size();
		if (picks.size() != Ndeme) {
			pagmo_throw(index_error,"mismatch between deme size and picks size while inserting best in deme");
		}
		size_t bestindeme = 0, worstinpicks = 0;
		double best = deme[0].getFitness(), worst = pop[picks[0]].getFitness();
		// Determine the best individual in deme and the worst among the picks.
		for (size_t i = 1; i < Ndeme; ++i) {
			if (deme[i].getFitness() < best) {
				bestindeme = i;
				best = deme[i].getFitness();
			}
			if (picks[i] >= pop_size) {
				pagmo_throw(index_error,"pick value exceeds population's size while inserting best in deme");
			}
			if (pop[picks[i]].getFitness() > worst) {
				worstinpicks = i;
				worst = pop[picks[i]].getFitness();
			}
		}
		// In place of the worst among the picks, insert the best in deme.
		pop[picks[worstinpicks]] = deme[bestindeme];

	}

	double Population::evaluateMean() const
	{
		if (pop.empty()) {
			pagmo_throw(index_error,"population is empty");
		}
		double mean = 0;
		const size_t size = pop.size();
		for (size_t i = 0; i < size; ++i) {
			mean += pop[i].getFitness();
		}
		mean /= (double)size;
		return mean;
	}

	double Population::evaluateStd() const
	{
		if (pop.empty()) {
			pagmo_throw(index_error,"population is empty");
		}
		double Std = 0, mean = evaluateMean();
		const size_t size = pop.size();
		for (size_t i = 0; i < size; ++i){
			Std += pow((pop[i].getFitness()-mean),2.0);
		}
		Std = sqrt(Std/size);
		return Std;
	}

	Individual &Population::operator[](const size_t &index)
	{
		return pop[index];
	};

	const Individual &Population::operator[](const size_t &index) const
	{
		return pop[index];
	};
