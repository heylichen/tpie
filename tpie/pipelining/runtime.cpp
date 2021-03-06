// -*- mode: c++; tab-width: 4; indent-tabs-mode: t; c-file-style: "stroustrup"; -*-
// vi:set ts=4 sts=4 sw=4 noet cino+=(0 :
// Copyright 2013, 2014, The TPIE development team
// 
// This file is part of TPIE.
// 
// TPIE is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
// 
// TPIE is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with TPIE.  If not, see <http://www.gnu.org/licenses/>

#include <tpie/fractional_progress.h>
#include <tpie/progress_indicator_null.h>
#include <tpie/disjoint_sets.h>
#include <tpie/pipelining/tokens.h>
#include <tpie/pipelining/node.h>
#include <tpie/pipelining/runtime.h>
#include <boost/functional/hash.hpp>
#include <iomanip>

namespace tpie {

namespace pipelining {

namespace bits {

struct not_a_dag_exception : public exception {
	not_a_dag_exception() : exception("") {}
};

///////////////////////////////////////////////////////////////////////////////
/// \brief  Directed graph with nodes of type T.
///
/// The node set is implied by the endpoints of the edges.
///
/// Computes the topological order using depth first search.
///////////////////////////////////////////////////////////////////////////////
template <typename T>
class graph {
public:
	void add_node(T v) {
		m_nodes.insert(v);
		m_edgeLists[v]; // ensure v has an edge list
	}

	void add_edge(T u, T v) {
		add_node(u);
		add_node(v);
		m_edgeLists[u].push_back(v);
	}

	void remove_edge(size_t u, size_t v) {
		auto & edges = m_edgeLists[u];
		auto it = std::find(edges.begin(), edges.end(), v);
		if (it != edges.end()) {
			edges.erase(it);
		}
	}

	const std::set<T> & get_node_set() const {
		return m_nodes;
	}

	const std::vector<T> & get_edge_list(const T & i) const {
		return m_edgeLists.find(i)->second;
	}

	size_t size() const {
		return m_nodes.size();
	}

	bool has_edge(T u, T v) const {
		const std::vector<T> & edgeList = m_edgeLists.find(u)->second;
		return std::find(edgeList.begin(), edgeList.end(), v) != edgeList.end();
	}

	[[nodiscard]] bool check_acyclical() {
		depth_first_search dfs(m_edgeLists);
		for (T v : m_nodes) {
			if (dfs.visit(v) == depth_first_search::BAD) return false;
		}
		return true;
	}

	void validate_acyclical() {
		if (!check_acyclical())
			throw not_a_dag_exception();
	}

	void topological_order(std::vector<T> & result) const {
		const size_t N = m_nodes.size();
		depth_first_search dfs(m_edgeLists);
		std::vector<std::pair<size_t, T> > nodes(N);
		for (auto i = m_edgeLists.begin(); i != m_edgeLists.end(); ++i) {
			auto r = dfs.visit(i->first);
			if (r == depth_first_search::BAD)
				throw not_a_dag_exception();

			nodes.push_back(std::make_pair(r, i->first));
		}
		std::sort(nodes.begin(), nodes.end(), std::greater<std::pair<size_t, T> >());
		result.resize(N);
		for (size_t i = 0; i < N; ++i) result[i] = nodes[i].second;
	}

	void rootfirst_topological_order(std::vector<T> & result) const { // a topological order where the root of trees are always visited first in the DFS
		std::vector<T> topologicalOrder;
		topological_order(topologicalOrder);

		const size_t N = m_nodes.size();
		depth_first_search dfs(m_edgeLists);
		std::vector<std::pair<size_t, T> > nodes(N);
		for (auto i = topologicalOrder.begin(); i != topologicalOrder.end(); ++i)
			nodes.push_back(std::make_pair(dfs.visit(*i), *i));
		std::sort(nodes.begin(), nodes.end(), std::greater<std::pair<size_t, T> >());
		result.resize(N);
		for (size_t i = 0; i < N; ++i) result[i] = nodes[i].second;
	}

	template <typename Compare>
	void sort_edge_list(T u, Compare comp) {
		std::sort(m_edgeLists[u].begin(), m_edgeLists[u].end(), comp);
	}

	/**
	 * \brief Returns the strongly connected components in a topological order
	 */
	std::vector<std::set<T>> strongly_connected_components() const {
		SCC scc(m_nodes, m_edgeLists);
		// Tarjan's algorithm finds the SCCs in a reverse topological order
		std::vector<std::set<T>> components = scc.get_components();
		std::reverse(components.begin(), components.end());
		return components;
	}

	void plot(std::ostream & out) {
		out << "digraph {\n";
		for (T u : get_node_set()) {
			out << u << '\n';
			for (T v : get_edge_list(u)) {
				out << u << " -> " << v << '\n';
			}
		}
		out << '}' << std::endl;
	}

private:
	std::set<T> m_nodes;
	std::map<T, std::vector<T> > m_edgeLists;

	class SCC {
	public:
		SCC(const std::set<T> & nodes, const std::map<T, std::vector<T> > & edgeLists)
			: m_nodes(nodes), m_edgeLists(edgeLists)
		{
		}

		std::vector<std::set<T>> get_components() {
			if (index == 0) {
				for (T u : m_nodes) {
					if (indices.find(u) == indices.end()) {
						visit(u);
					}
				}
			}
			return components;
		}

	private:
		void visit(T u) {
			indices[u] = index;
			lowlinks[u] = index;
			index++;
			S.push(u);
			onStack[u] = true;

			for (T v : get_edge_list(u)) {
				if (indices.find(v) == indices.end()) {
					visit(v);
					lowlinks[u] = std::min(lowlinks[u], lowlinks[v]);
				} else if (onStack[v]) {
					lowlinks[u] = std::min(lowlinks[u], lowlinks[v]);
				}
			}

			if (indices[u] == lowlinks[u]) {
				std::set<T> component;
				T v;
				do {
					v = S.top();
					S.pop();
					onStack[v] = false;
					component.insert(v);
				} while (v != u);
				components.push_back(component);
			}
		}

		const std::vector<T> & get_edge_list(T u) {
			auto i = m_edgeLists.find(u);
			if (i == m_edgeLists.end())
				throw tpie::exception("get_edge_list: no such node");
			return i->second;
		}

		size_t index = 0;
		std::stack<T> S;
		std::map<T, size_t> indices;
		std::map<T, size_t> lowlinks;
		std::map<T, bool> onStack;

		std::vector<std::set<T>> components;

		const std::set<T> & m_nodes;
		const std::map<T, std::vector<T> > & m_edgeLists;
	};

	class depth_first_search {
	public:
		static constexpr size_t BAD = std::numeric_limits<size_t>::max();
		
		depth_first_search(const std::map<T, std::vector<T> > & edgeLists)
			: m_time(0)
			, m_edgeLists(edgeLists)
		{
		}

		[[nodiscard]] size_t visit(T u) {
			if (m_finishTime.count(u)) {
				if (m_finishTime[u] == 0)
					return BAD;
				return m_finishTime[u];
			}
			m_finishTime[u] = 0;
			++m_time;
			const std::vector<T> & edgeList = get_edge_list(u);
			for (size_t i = 0; i < edgeList.size(); ++i)
				if (visit(edgeList[i]) == BAD)
					return BAD;
			return m_finishTime[u] = m_time++;
		}

	private:
		const std::vector<T> & get_edge_list(T u) {
			auto i = m_edgeLists.find(u);
			if (i == m_edgeLists.end())
				throw tpie::exception("get_edge_list: no such node");
			return i->second;
		}

		size_t m_time;
		const std::map<T, std::vector<T> > & m_edgeLists;
		std::map<T, size_t> m_finishTime;
	};
};

class satisfiable_graph {
	typedef size_t node_t;

	graph<node_t> m_graph;
	std::set<std::pair<node_t, node_t>> m_satisfiableEdges;

public:
	void add_node(node_t u) {
		m_graph.add_node(u);
	}

	void add_edge(node_t u, node_t v, bool satisfiable) {
		m_graph.add_edge(u, v);
		if (satisfiable) {
			m_satisfiableEdges.insert({u, v});
		}
	}

	void remove_edge(node_t u, node_t v) {
		m_graph.remove_edge(u, v);
		m_satisfiableEdges.erase({u ,v});
	}

	const std::set<node_t> & get_node_set() const {
		return m_graph.get_node_set();
	}

	void plot(std::ostream & out) {
		out << "digraph {\n";
		for (node_t u : get_node_set()) {
			out << u << '\n';
			for (node_t v : m_graph.get_edge_list(u)) {
				out << u << " -> " << v << " " << (m_satisfiableEdges.count({u, v})? "[color=red]": "") << '\n';
			}
		}
		out << '}' << std::endl;
	}

	static constexpr size_t max_bruteforce_depth = 10;
	static constexpr size_t max_bruteforce_satisfiable = 18;

private:
	/**
	 * \brief Returns the number of paths from u to all othe nodes
	 *
	 * paths(u, cache)[v] is the number of paths from u to v
	 */
	std::unordered_map<node_t, size_t> & paths(node_t u, std::unordered_map<node_t, std::unordered_map<node_t, size_t>> & cache) {
		if (cache[u].size()) {
			return cache[u];
		}

		std::unordered_map<node_t, size_t> result;
		for (node_t v : m_graph.get_edge_list(u)) {
			result[v]++;
			for (node_t w : get_node_set()) {
				result[w] += paths(v, cache)[w];
			}
		}

		return cache[u] = result;
	}

	/**
	 * \brief Removes all unnecessary edges
	 *
	 * An edge (u, v) is unnecessary if there exists another path between u and v
	 */
	void preprocess() {
		m_graph.validate_acyclical();

		std::unordered_map<node_t, std::unordered_map<node_t, size_t>> cache;

		for (node_t u : get_node_set()) {
			std::vector<node_t> unnecessaryEdges;
			for (node_t v : m_graph.get_edge_list(u)) {
				// If there exists another path between u and v, remove it
				if (paths(u, cache)[v] > 1) {
					unnecessaryEdges.push_back(v);
				}
			}
			for (size_t v : unnecessaryEdges) {
				remove_edge(u, v);
			}
		}
	}

	/**
	 * \brief Returns the subgraph only containing the specified nodes
	 */
	satisfiable_graph subgraph(std::set<node_t> & nodes) const {
		satisfiable_graph g;
		for (node_t u : nodes) {
			g.add_node(u);
			for (node_t v : m_graph.get_edge_list(u)) {
				if (nodes.count(v)) {
					g.add_edge(u, v, (bool)m_satisfiableEdges.count({u, v}));
				}
			}
		}
		return g;
	}

	/**
	 * \brief Splits the graph into smaller independent subgraphs
	 *
	 * We do this by finding cuts in the graph that doesn't contain satisfiable edges
	 * and where all edges points in the same direction across the cut.
	 * We can use these subgraphs to satisfy the maximum number of edges faster.
	 */
	std::vector<satisfiable_graph> split_graph() const {
		graph<node_t> sccGraph = m_graph;
		for (const auto & p : m_satisfiableEdges) {
			sccGraph.add_edge(p.second, p.first);
		}
		std::vector<satisfiable_graph> result;
		for (std::set<node_t> & component : sccGraph.strongly_connected_components()) {
			result.push_back(subgraph(component));
		}
		return result;
	}

	/**
	 * \brief Gives a lower bound on the maximum number of satisfiable edges
	 */
	size_t minimum_satisfiable_edges() {
		return m_satisfiableEdges.size() == 0? 0: 1;
	}

	struct result_t {
		size_t satisfied;
		std::vector<node_t> order;
	};

	result_t bruteforce_optimal_topological_order_helper(
		std::unordered_map<node_t, size_t> & indegrees,
		std::unordered_set<node_t> & roots,
		std::vector<node_t> & order)
	{
		if (order.size() == m_graph.size()) {
			return {0, order};
		}

		tp_assert(roots.size() > 0, "No nodes with indegree 0!");

		result_t best;
		best.satisfied = 0;
		bool first = true;
		auto rootsCopy = roots;
		for (node_t u : rootsCopy) {
			bool satisfiedEdge = order.size() > 0 && m_satisfiableEdges.count({*order.rbegin(), u});

			roots.erase(u);

			std::vector<node_t> newRoots;
			for (node_t v : m_graph.get_edge_list(u)) {
				size_t & indegree = indegrees[v];
				indegree--;
				if (indegree == 0) {
					roots.insert(v);
					newRoots.push_back(v);
				}
			}

			order.push_back(u);

			result_t result = bruteforce_optimal_topological_order_helper(indegrees, roots, order);

			result.satisfied += satisfiedEdge;
			if (first || result.satisfied > best.satisfied) {
				best = std::move(result);
				first = false;

				if (best.satisfied == m_satisfiableEdges.size()) {
					return best;
				}
			}

			order.pop_back();

			for (node_t v : newRoots) {
				roots.erase(v);
			}
			for (node_t v : m_graph.get_edge_list(u)) {
				size_t & indegree = indegrees[v];
				indegree++;
			}

			roots.insert(u);
		}

		return best;
	}

	/*
	 * Runs in O*(n!)
	 */
	void bruteforce_optimal_topological_order(std::vector<node_t> & order) {
		std::unordered_map<node_t, size_t> indegrees;
		for (node_t u : get_node_set()) {
			for (node_t v : m_graph.get_edge_list(u)) {
				indegrees[v]++;
			}
		}

		std::unordered_set<node_t> roots;
		for (node_t u : get_node_set()) {
			if (indegrees[u] == 0) {
				roots.insert(u);
			}
		}

		std::vector<node_t> recOrder;
		result_t result = bruteforce_optimal_topological_order_helper(indegrees, roots, recOrder);

		order = result.order;
	}

	/*
	 * Runs in O*(2^k), where k is the number of satisfiable edges
	 */
	void bruteforce_satisfiable_edges(std::vector<node_t> & order) {
		size_t N = m_graph.size();
		size_t M = m_satisfiableEdges.size();

		tp_assert(M <= sizeof(size_t) * CHAR_BIT, "Too many satisfiable edges");

		std::unordered_map<node_t, size_t> nodeIndices;
		std::unordered_map<size_t, node_t> revNodeIndices;
		{
			size_t i = 0;
			for (node_t u : get_node_set()) {
				nodeIndices[u] = i;
				revNodeIndices[i] = u;
				i++;
			}
		}

		bool noBest = true;
		size_t bestSatisfied = 0;
		disjoint_sets<size_t> bestContractedNodes;
		std::unordered_map<size_t, graph<size_t>> bestContractedPaths;
		graph<size_t> bestContractedGraph;

		size_t minimumSatisfiable = minimum_satisfiable_edges();

		size_t combinations = 1 << M;
		for (size_t i = 0; i < combinations; i++) {
			disjoint_sets<size_t> contractedNodes(N);
			for (size_t j = 0; j < N; j++) {
				contractedNodes.make_set(j);
			}

			std::unordered_set<node_t> satisfiedOut;
			std::unordered_set<node_t> satisfiedIn;

			bool bad = false;
			size_t satisfied = 0;
			size_t j = 0;
			for (const auto & p : m_satisfiableEdges) {
				if ((1 << j) & i) {
					size_t k = nodeIndices[p.first];
					size_t l = nodeIndices[p.second];
					contractedNodes.union_set(k, l);
					if (satisfiedOut.count(k) || satisfiedIn.count(l)) {
						bad = true;
						break;
					}
					satisfiedOut.insert(k);
					satisfiedIn.insert(l);
					satisfied++;
				}
				j++;
			}

			if (bad) continue;

			if (satisfied < minimumSatisfiable) continue;

			std::unordered_map<size_t, graph<size_t>> contractedPaths;
			j = 0;
			for (const auto & p : m_satisfiableEdges) {
				if ((1 << j) & i) {
					size_t k = contractedNodes.find_set(nodeIndices[p.first]);
					contractedPaths[k].add_edge(nodeIndices[p.first], nodeIndices[p.second]);
				}
				j++;
			}

			graph<size_t> contractedGraph;
			for (size_t j = 0; j < N; j++) {
				contractedGraph.add_node(contractedNodes.find_set(j));
			}

			for (node_t u : get_node_set()) {
				size_t j = contractedNodes.find_set(nodeIndices[u]);
				for (node_t v : m_graph.get_edge_list(u)) {
					size_t k = contractedNodes.find_set(nodeIndices[v]);
					if (j != k) {
						contractedGraph.add_edge(j, k);
					}
				}
			}

			if (!contractedGraph.check_acyclical()) continue;

			if (noBest || satisfied > bestSatisfied) {
				noBest = false;

				bestSatisfied = satisfied;
				bestContractedNodes = std::move(contractedNodes);
				bestContractedPaths = std::move(contractedPaths);
				bestContractedGraph = std::move(contractedGraph);

				if (bestSatisfied == m_satisfiableEdges.size()) {
					break;
				}
			}
		}

		tp_assert(!noBest, "Couldn't find any best solution!");

		std::vector<size_t> indexOrder;
		bestContractedGraph.topological_order(indexOrder);

		for (const auto & p : bestContractedPaths) {
			size_t i = p.first;
			const graph<size_t> & g = p.second;

			std::vector<size_t> path;
			g.topological_order(path);

			auto it = std::find(indexOrder.begin(), indexOrder.end(), i);
			*it = *path.rbegin();
			indexOrder.insert(it, path.begin(), path.end() - 1);
		}

		for (size_t i : indexOrder) {
			order.push_back(revNodeIndices[i]);
		}
	}

	void greedy_topological_order(std::vector<node_t> & order) {
		// Make the satisfiable edges be last in the edge lists
		for (node_t u : get_node_set()) {
			m_graph.sort_edge_list(u, [&](node_t a, node_t b){
				return m_satisfiableEdges.count({u, a}) < m_satisfiableEdges.count({u, b});
			});
		}

		m_graph.rootfirst_topological_order(order);
	}

	void auto_topological_order(std::vector<node_t> & order) {
		static_assert(max_bruteforce_satisfiable <= sizeof(size_t) * CHAR_BIT, "max_bruteforce_satisfiable is too big");
		if (m_satisfiableEdges.size() <= max_bruteforce_satisfiable) {
			bruteforce_satisfiable_edges(order);
			return;
		}

		if (m_graph.size() <= max_bruteforce_depth) {
			bruteforce_optimal_topological_order(order);
			return;
		}
		greedy_topological_order(order);
	}

public:
	enum strategy_t {
		BRUTEFORCE_ORDER,
		BRUTEFORCE_SATISFIABLE,
		GREEDY,
		AUTO,
	};

	void topological_order(std::vector<node_t> & order, strategy_t strategy = AUTO) {
		void (satisfiable_graph::* get_order)(std::vector<node_t> &);
		switch (strategy) {
		case BRUTEFORCE_ORDER: get_order = &satisfiable_graph::bruteforce_optimal_topological_order; break;
		case BRUTEFORCE_SATISFIABLE: get_order = &satisfiable_graph::bruteforce_satisfiable_edges; break;
		case GREEDY: get_order = &satisfiable_graph::greedy_topological_order; break;
		case AUTO: get_order = &satisfiable_graph::auto_topological_order; break;
		default: get_order = nullptr; throw tpie::exception("Unknown strategy");
		}

		preprocess();

		order.clear();

		std::vector<satisfiable_graph> subgraphs = split_graph();
		for (satisfiable_graph & g : subgraphs) {
			g.preprocess();
			std::vector<node_t> subOrder;
			(g.*get_order)(subOrder);

			order.insert(order.end(), subOrder.begin(), subOrder.end());
		}
	}

	/**
	 * \brief Counts the number of satisfied edges in a topological order
	 */
	size_t satisfied_in_order(std::vector<node_t> & order) const {
		if (order.size() == 0) {
			return 0;
		}

		size_t result = 0;
		for (size_t i = 0; i < order.size() - 1; i++) {
			result += m_satisfiableEdges.count({order[i], order[i + 1]});
		}
		return result;
	}
};

class resource_runtime {
public:
	resource_runtime(const std::vector<node *> & nodes, resource_type type)
	: m_nodes(nodes)
	, m_minimumUsage(0)
	, m_maximumUsage(0)
	, m_fraction(0.0)
	, m_type(type)
	{
		const size_t N = m_nodes.size();
		for (size_t i = 0; i < N; ++i) {
			m_minimumUsage += minimum_usage(i);
			m_maximumUsage += maximum_usage(i);
			m_fraction += fraction(i);
		}
	}

	// Node accessors
	memory_size_type minimum_usage(size_t i) const {
		return m_nodes[i]->get_minimum_resource_usage(m_type);
	};
	memory_size_type maximum_usage(size_t i) const {
		return m_nodes[i]->get_maximum_resource_usage(m_type);
	};
	double fraction(size_t i) const {
		return m_nodes[i]->get_resource_fraction(m_type);
	};

	// Node accessor aggregates
	memory_size_type sum_minimum_usage() const {
		return m_minimumUsage;
	};
	memory_size_type sum_maximum_usage() const {
		return m_maximumUsage;
	};
	double sum_fraction() const {
		return m_fraction;
	};

	// Node mutator
	void set_usage(size_t i, memory_size_type usage) {
		m_nodes[i]->_internal_set_available_of_resource(m_type, usage);
	};

	void assign_usage(double factor) {
		for (size_t i = 0; i < m_nodes.size(); ++i)
			set_usage(i, get_assigned_usage(i, factor));
	};

	// Special case of assign_usage when factor is zero.
	void assign_minimum_resource() {
		for (size_t i = 0; i < m_nodes.size(); ++i)
			set_usage(i, minimum_usage(i));
	};

	memory_size_type sum_assigned_usage(double factor) const {
		memory_size_type total = 0;
		for (size_t i = 0; i < m_nodes.size(); ++i)
			total += get_assigned_usage(i, factor);
		return total;
	};

	memory_size_type get_assigned_usage(size_t i, double factor) const {
		return clamp(minimum_usage(i), maximum_usage(i), factor * fraction(i));
	};

	static memory_size_type clamp(memory_size_type lo, memory_size_type hi,
								  double v) {
		if (v < lo) return lo;
		if (v > hi) return hi;
		return static_cast<memory_size_type>(v);
	};

	void print_usage(double c, std::ostream & os) {
		size_t cw = 12;
		size_t prec_frac = 2;
		std::string sep(2, ' ');

		os	<< "\nPipelining phase " << m_type << " assigned\n"
			<< std::setw(cw) << "Minimum"
			<< std::setw(cw) << "Maximum"
			<< std::setw(cw) << "Fraction"
			<< std::setw(cw) << "Assigned"
			<< sep << "Name\n";

		for (size_t i = 0; i < m_nodes.size(); ++i) {
			std::string frac;
			{
				std::stringstream ss;
				ss << std::fixed << std::setprecision(prec_frac)
					<< fraction(i);
				frac = ss.str();
			}

			stream_size_type lo = minimum_usage(i);
			stream_size_type hi = maximum_usage(i);
			stream_size_type assigned = get_assigned_usage(i, c);

			os	<< std::setw(cw) << lo;
			if (hi == std::numeric_limits<stream_size_type>::max()) {
				os << std::setw(cw) << "inf";
			} else {
				os << std::setw(cw) << hi;
			}
			os	<< std::setw(cw) << frac
				<< std::setw(cw) << assigned
				<< sep
				<< m_nodes[i]->get_name().substr(0, 50) << '\n';
		}
		os << std::endl;
	}

protected:
	const std::vector<node *> & m_nodes;
	memory_size_type m_minimumUsage;
	memory_size_type m_maximumUsage;
	double m_fraction;
	resource_type m_type;
};

///////////////////////////////////////////////////////////////////////////////
/// Helper methods for file assignment.
/// The file assignment algorithm is in runtime::get_files_factor.
///////////////////////////////////////////////////////////////////////////////
class file_runtime : public resource_runtime {
public:
	file_runtime(const std::vector<node *> & nodes) : resource_runtime(nodes, FILES) {}
};

///////////////////////////////////////////////////////////////////////////////
/// Helper methods for memory assignment.
/// The memory assignment algorithm is in runtime::get_memory_factor.
///////////////////////////////////////////////////////////////////////////////
class memory_runtime : public resource_runtime {
public:
	memory_runtime(const std::vector<node *> & nodes) : resource_runtime(nodes, MEMORY) {}
};

///////////////////////////////////////////////////////////////////////////////
/// Helper methods for memory assignment.
/// The memory assignment algorithm is in runtime::get_memory_factor.
///////////////////////////////////////////////////////////////////////////////
class datastructure_runtime {
public:
	datastructure_runtime(const std::vector<std::vector<node *> > & phases, node_map & nodeMap);

	memory_size_type sum_minimum_memory(size_t phase) const; // sum the minimum memory for datastructures used in the phase
	double sum_fraction(size_t i) const; // sum the fractions for datastructures used in phase i
	memory_size_type sum_assigned_memory(double factor, size_t phase) const; // sum the assigned memory for datastructures used in the phase
	void minimize_factor(double factor, size_t phase); // the factor for the datastructure in the phase is set to be no higher than the given factor
	memory_size_type sum_assigned_memory(size_t phase) const; // sum the assigned memory for datastructures used in the phase using the factors given to the minimize_factor method
	void assign_memory();

	void free_datastructures(size_t phase) {
		auto & ds = m_nodeMap.get_datastructures();
		for (auto & p: m_datastructures) {
			if (p.second.right_most_phase != phase) continue;
			auto it = ds.find(p.first);
			if (it == ds.end()) continue;
			it->second.second.reset();
		}
	}
	
	//void print_memory(double c, std::ostream & os);
private:
	static memory_size_type clamp(memory_size_type lo, memory_size_type hi, double v);

	struct datastructure_info_t {
		memory_size_type min;
		memory_size_type max;
		double priority;
		memory_size_type right_most_phase;
		memory_size_type left_most_phase;
		double factor;

		datastructure_info_t()
		: min(0)
		, max(std::numeric_limits<memory_size_type>::max())
		, priority(1)
		, right_most_phase(0)
		, left_most_phase(std::numeric_limits<memory_size_type>::max())
		, factor(std::numeric_limits<double>::max())
		{}
	};

	std::map<std::string, datastructure_info_t> m_datastructures;
	node_map & m_nodeMap;
};

std::string get_phase_name(const std::vector<node *> & phase) {
	priority_type highest = std::numeric_limits<priority_type>::lowest();
	size_t highest_node = 0;
	for (size_t i = 0; i < phase.size(); ++i) {
		if (phase[i]->get_phase_name_priority() > highest && phase[i]->get_phase_name().size()) {
			highest_node = i;
			highest = phase[i]->get_phase_name_priority();
		}
	}
	std::string n = phase[highest_node]->get_phase_name();
	if (!n.empty()) return n;

	highest_node = 0;
	for (size_t i = 0; i < phase.size(); ++i) {
		if (phase[i]->get_name_priority() > highest) {
			highest_node = i;
			highest = phase[i]->get_name_priority();
		}
	}
	return phase[highest_node]->get_name();
}

	
///////////////////////////////////////////////////////////////////////////////
/// \brief  Helper class for RAII-style progress indicators.
///
/// init calls fractional_progress::init,
/// and the destructor calls fractional_progress::done.
///
/// Instantiate phase_progress_indicators
/// to call init and done on subindicators.
///////////////////////////////////////////////////////////////////////////////
class progress_indicators {
public:
	progress_indicators(): m_nulls(false) {}

	progress_indicators(const progress_indicators & o) = delete;
	progress_indicators & operator =(const progress_indicators & o) = delete;

	progress_indicators(progress_indicators && o) = default;
	progress_indicators & operator =(progress_indicators && o) = default;

	~progress_indicators() {
		if (!std::uncaught_exceptions() && fp) fp->done();
	}

	void init(stream_size_type n,
			  progress_indicator_base & pi,
			  const std::vector<std::vector<node *> > & phases,
			  const char * file,
			  const char * function) {
		const size_t N = phases.size();
		m_progressIndicators.resize(N);
		fp = nullptr;
		if (!file|| !function) {
			m_nulls = true;
			for (size_t i = 0; i < N; ++i) 
				m_progressIndicators[i] = std::make_unique<progress_indicator_null>();
			return;
		}
		m_nulls = false;
		
		fp = std::make_unique<fractional_progress>(&pi);
		std::size_t uuid = 0;
		for (size_t i = 0; i < N; ++i) {
			for (node * n: phases[i])
				boost::hash_combine(uuid, n->get_name());
			std::string name = get_phase_name(phases[i]);
			char id[128];
			// since an int like i cannot be 17 chars long, this cannot overflow
			sprintf(id, "p%03d:%.100s:%08llX", (int)i, name.c_str(), (unsigned long long)uuid);
			m_progressIndicators[i] = std::make_unique<fractional_subindicator>(
				*fp, id, file, function, n, name.c_str());
		}
		fp->init();
	}

private:
	friend class phase_progress_indicator;

	std::unique_ptr<fractional_progress> fp;
	bool m_nulls;
	std::vector<std::unique_ptr<progress_indicator_base> > m_progressIndicators;
};

///////////////////////////////////////////////////////////////////////////////
/// RAII-style progress indicator for a single phase.
/// Constructor computes number of steps and calls init; destructor calls done.
///////////////////////////////////////////////////////////////////////////////
class phase_progress_indicator {
public:
	phase_progress_indicator() : m_pi(nullptr) {}
	phase_progress_indicator(const phase_progress_indicator &) = delete;
	phase_progress_indicator(phase_progress_indicator && o): m_pi(o.m_pi) {o.m_pi = nullptr;}

	phase_progress_indicator & operator=(const phase_progress_indicator &) = delete;
	phase_progress_indicator & operator=(phase_progress_indicator && o) {
		if (m_pi) m_pi->done(); 
		m_pi = o.m_pi;
		o.m_pi = nullptr;
		return *this;
	}

	phase_progress_indicator(progress_indicators & pi, size_t phaseNumber,
							 const std::vector<node *> & nodes, bool emptyFace)
		: m_pi(pi.m_progressIndicators[phaseNumber].get())
	{
		if (emptyFace && !pi.m_nulls)
			static_cast<fractional_subindicator*>(m_pi)->set_crumb("");
		stream_size_type steps = 0;
		for (size_t j = 0; j < nodes.size(); ++j) {
			steps += nodes[j]->get_steps();
		}
		m_pi->init(steps);
	}

	~phase_progress_indicator() {
		if (!std::uncaught_exceptions() && m_pi) m_pi->done();
	}

	progress_indicator_base & get() {
		return *m_pi;
	}

private:
	progress_indicator_base * m_pi;
};

///////////////////////////////////////////////////////////////////////////////
/// begin/end handling on nodes.
///////////////////////////////////////////////////////////////////////////////
class begin_end {
public:
	begin_end(graph<node *> & actorGraph) {
		actorGraph.topological_order(m_topologicalOrder);
	}

	void begin() {
		for (size_t i = m_topologicalOrder.size(); i--;) {
			m_topologicalOrder[i]->set_state(node::STATE_IN_BEGIN);
			m_topologicalOrder[i]->begin();
			m_topologicalOrder[i]->set_state(node::STATE_AFTER_BEGIN);
		}
	}

	void end() {
		for (size_t i = 0; i < m_topologicalOrder.size(); ++i) {
			m_topologicalOrder[i]->set_state(node::STATE_IN_END);
			m_topologicalOrder[i]->end();
			m_topologicalOrder[i]->set_state(node::STATE_AFTER_END);
		}
	}

private:
	std::vector<node *> m_topologicalOrder;
};

datastructure_runtime::datastructure_runtime(const std::vector<std::vector<node *> > & phases, node_map & nodeMap)
	: m_nodeMap(nodeMap)
{
	for (size_t phase = 0; phase < phases.size(); ++phase) {
		for (std::vector<node *>::const_iterator node = phases[phase].begin(); node != phases[phase].end(); ++node) {
			const node::datastructuremap_t & node_datastructures = (*node)->get_datastructures();
			for (node::datastructuremap_t::const_iterator datastructure = node_datastructures.begin(); datastructure != node_datastructures.end(); ++datastructure) {
				const std::string & name = datastructure->first;
				const node::datastructure_info_t & info = datastructure->second;

				std::map<std::string, datastructure_info_t>::iterator i = m_datastructures.find(name);
				if (i == m_datastructures.end()) {
					datastructure_info_t agg_info;
					agg_info.min = info.min;
					agg_info.max = info.max;
					agg_info.priority = info.priority;
					agg_info.left_most_phase = phase;
					agg_info.right_most_phase = phase;
					m_datastructures[name] = agg_info;
					continue;
				}

				datastructure_info_t & agg_info = i->second;
				agg_info.min = std::max(agg_info.min, info.min);
				agg_info.max = std::min(agg_info.max, info.max);
				agg_info.priority = std::min(agg_info.priority, info.priority);
				agg_info.right_most_phase = phase;
			}
		}
	}
}

memory_size_type datastructure_runtime::sum_minimum_memory(size_t phase) const {
	memory_size_type r = 0;
	for(std::map<std::string, datastructure_info_t>::const_iterator i = m_datastructures.begin(); i != m_datastructures.end(); ++i) {
		const datastructure_info_t & agg_info = i->second;
		if(agg_info.left_most_phase <= phase && phase <= agg_info.right_most_phase)
			r += agg_info.min;
	}

	return r;
}

double datastructure_runtime::sum_fraction(size_t phase) const {
	double r = 0.0;
	for(std::map<std::string, datastructure_info_t>::const_iterator i = m_datastructures.begin(); i != m_datastructures.end(); ++i) {
		const datastructure_info_t & agg_info = i->second;
		if(agg_info.left_most_phase <= phase && phase <= agg_info.right_most_phase)
			r += agg_info.priority;
	}

	return r;
}

memory_size_type datastructure_runtime::sum_assigned_memory(double factor, size_t phase) const {
	memory_size_type r = 0;
	for(std::map<std::string, datastructure_info_t>::const_iterator i = m_datastructures.begin(); i != m_datastructures.end(); ++i) {
		const datastructure_info_t & agg_info = i->second;
		if(agg_info.left_most_phase <= phase && phase <= agg_info.right_most_phase)
			r += clamp(agg_info.min, agg_info.max, agg_info.priority * factor);
	}

	return r;
}

void datastructure_runtime::minimize_factor(double factor, size_t phase) {
	for(std::map<std::string, datastructure_info_t>::iterator i = m_datastructures.begin(); i != m_datastructures.end(); ++i) {
		datastructure_info_t & agg_info = i->second;
		if(agg_info.left_most_phase <= phase && phase <= agg_info.right_most_phase)
			agg_info.factor = std::min(agg_info.factor, factor);
	}
}

memory_size_type datastructure_runtime::sum_assigned_memory(size_t phase) const {
	memory_size_type r = 0;
	for(std::map<std::string, datastructure_info_t>::const_iterator i = m_datastructures.begin(); i != m_datastructures.end(); ++i) {
		const datastructure_info_t & agg_info = i->second;
		if(agg_info.left_most_phase <= phase && phase <= agg_info.right_most_phase)
			r += clamp(agg_info.min, agg_info.max, agg_info.priority * agg_info.factor);
	}

	return r;
}

memory_size_type datastructure_runtime::clamp(memory_size_type lo, memory_size_type hi, double v) {
	if(v < lo) return lo;
	if(v > hi) return hi;
	return static_cast<memory_size_type>(v);
}

void datastructure_runtime::assign_memory() {
	for(std::map<std::string, datastructure_info_t>::iterator i = m_datastructures.begin(); i != m_datastructures.end(); ++i) {
		memory_size_type mem = clamp(i->second.min, i->second.max, i->second.factor * i->second.priority);
		m_nodeMap.get_datastructures().insert(std::make_pair(i->first, std::make_pair(mem, any_noncopyable())));
	}
}

struct gocontext {
	std::map<node *, size_t> phaseMap;
	graph<size_t> phaseGraph;
	std::vector<std::vector<node *> > phases;
	std::unordered_set<node_map::id_t> evacuateWhenDone;
	std::vector<graph<node *> > itemFlow;
	std::vector<graph<node *> > actor;
	datastructure_runtime drt;
	progress_indicators pi;
	size_t i;
	memory_size_type files;
	memory_size_type memory;
	phase_progress_indicator phaseProgress;
};


runtime::runtime(node_map::ptr nodeMap)
	: m_nodeMap(*nodeMap)
{
}

size_t runtime::get_node_count() {
	return m_nodeMap.size();
}


void gocontextdel::operator()(void * p) {delete static_cast<gocontext*>(p);}
	
gocontext_ptr runtime::go_init(stream_size_type items,
							 progress_indicator_base & progress,
							 memory_size_type files,
							 memory_size_type memory,
							 const char * file, const char * function) {
	if (get_node_count() == 0)
		throw tpie::exception("no nodes in pipelining graph");

	// Partition nodes into phases (using union-find)
	std::map<node *, size_t> phaseMap;
	get_phase_map(phaseMap);
	if (phaseMap.size() != get_node_count())
		throw tpie::exception("get_phase_map did not return "
							  "correct number of nodes");

	// Build phase graph
	graph<size_t> phaseGraph;
	get_phase_graph(phaseMap, phaseGraph);

	// Build phases vector

	std::vector<std::vector<node *> > phases;
	std::unordered_set<node_map::id_t> evacuateWhenDone;
	get_phases(phaseMap, phaseGraph, evacuateWhenDone, phases);

	// Build item flow graph and actor graph for each phase
	std::vector<graph<node *> > itemFlow;
	get_item_flow_graphs(phases, itemFlow);
	std::vector<graph<node *> > actor;
	get_actor_graphs(phases, actor);

	// Make the nodeMap forward all the forwards calls
	// made on pipe_bases
	m_nodeMap.forward_pipe_base_forwards();
	
	// Toposort item flow graph for each phase
	// and call node::prepare in item source to item sink order
	prepare_all(itemFlow);

	// build the datastructure runtime
	datastructure_runtime drt(phases, m_nodeMap); 

	// Gather node file requirements and assign files to each phase
	assign_files(phases, files);

	// Gather node memory requirements and assign memory to each phase
	assign_memory(phases, memory, drt);

	// Exception guarantees are the following:
	//   Progress indicators:
	//     We use RAII to match init() calls with done() calls.
	//     This means that we call done() on a progress indicator
	//     during stack unwinding if an exception is thrown.
	//   begin() and end():
	//     If an exception is thrown by an initiator,
	//     we do not call end() even though we called begin().
	//     This is to signal to the nodes that processing was aborted.
	//     A node may do finalization cleanup in its destructor
	//     rather than in end() to handle exceptions robustly.

	// Construct fractional progress indicators:
	// Get the name of each phase and call init() on the given indicator.
	progress_indicators pi;
	pi.init(items, progress, phases, file, function);

	return gocontext_ptr(new gocontext{
			std::move(phaseMap),
				std::move(phaseGraph),
				std::move(phases),
				std::move(evacuateWhenDone),
				std::move(itemFlow),
				std::move(actor),
				std::move(drt),
				std::move(pi),
				0,
				files,
				memory,
				phase_progress_indicator()});
}
	

void runtime::go_until(gocontext * gc, node * node) {
	if (gc->i > gc->phases.size()) return;
	
	if (gc->i != 0) {
		begin_end beginEnd(gc->actor[gc->i-1]);
		beginEnd.end();
	}

	for (; gc->i < gc->phases.size(); ++gc->i) {
		// Run each phase:
		// Evacuate previous if necessary
		auto & phase = gc->phases[gc->i];
		log_debug() << "Running pipe phase " << get_phase_name(phase) << std::endl;
		
		if (gc->i > 0) evacuate_all(gc->phases[gc->i-1], gc->evacuateWhenDone);
			
		// call propagate in item source to item sink order
		propagate_all(gc->itemFlow[gc->i]);
		// reassign files to all nodes in the phase
		reassign_files(gc->phases, gc->i, gc->files);
		// reassign memory to all nodes in the phase
		reassign_memory(gc->phases, gc->i, gc->memory, gc->drt);

		bool emptyFace = true;
		for (auto n: phase)
			if (is_initiator(n) && !n->is_go_free())
				emptyFace = false;
		
		// sum number of steps and call pi.init()
		gc->phaseProgress = phase_progress_indicator(gc->pi, gc->i, phase, emptyFace);
		
		// set progress indicators on each node
		set_progress_indicators(phase, gc->phaseProgress.get());
		// call begin in leaf to root actor order
		begin_end beginEnd(gc->actor[gc->i]);
		beginEnd.begin();
		
		// call go on initiators
		for (auto n: phase)
			if (n == node) {
				gc->i++;
				return;
			}
		go_initiators(gc->phases[gc->i]);

		// call end in root to leaf actor order
		beginEnd.end();

		gc->drt.free_datastructures(gc->i);

		// call pi.done in ~phase_progress_indicator
		gc->phaseProgress = phase_progress_indicator();
	}
	// call fp->done in ~progress_indicators
	gc->i++;
}
		
void runtime::go(stream_size_type items,
				 progress_indicator_base & progress,
				 memory_size_type filesAvailable,
				 memory_size_type memory,
				 const char * file,
				 const char * function) {
	gocontext_ptr gc = go_init(items, progress, filesAvailable, memory, file, function);
	// Check that each phase has at least one initiator
	ensure_initiators(gc->phases);
	go_until(gc.get(), nullptr);
}

void runtime::get_item_sources(std::vector<node *> & itemSources) {
	typedef node_map::id_t id_t;
	std::set<id_t> possibleSources;
	for (node_map::mapit i = m_nodeMap.begin(); i != m_nodeMap.end(); ++i) {
		possibleSources.insert(i->first);
	}
	const node_map::relmap_t & relations = m_nodeMap.get_relations();
	for (node_map::relmapit i = relations.begin(); i != relations.end(); ++i) {
		id_t from = i->first;
		id_t to = i->second.first;
		bits::node_relation rel = i->second.second;

		switch (rel) {
			case pushes:
				possibleSources.erase(to);
				break;
			case pulls:
			case depends:
			case no_forward_depends:
			case memory_share_depends:
				possibleSources.erase(from);
				break;
		}
	}
	for (std::set<id_t>::iterator i = possibleSources.begin();
		 i != possibleSources.end(); ++i) {
		itemSources.push_back(m_nodeMap.get(*i));
	}
}

void runtime::get_item_sinks(std::vector<node *> & itemSinks) {
	typedef node_map::id_t id_t;
	std::set<id_t> possibleSinks;
	for (node_map::mapit i = m_nodeMap.begin(); i != m_nodeMap.end(); ++i) {
		possibleSinks.insert(i->first);
	}
	const node_map::relmap_t & relations = m_nodeMap.get_relations();
	for (node_map::relmapit i = relations.begin(); i != relations.end(); ++i) {
		id_t from = i->first;
		id_t to = i->second.first;
		bits::node_relation rel = i->second.second;

		switch (rel) {
			case pushes:
				possibleSinks.erase(from);
				break;
			case pulls:
			case depends:
			case no_forward_depends:
			case memory_share_depends:
				possibleSinks.erase(to);
				break;
		}
	}
	for (std::set<id_t>::iterator i = possibleSinks.begin();
		 i != possibleSinks.end(); ++i) {
		itemSinks.push_back(m_nodeMap.get(*i));
	}
}

void runtime::get_phase_map(std::map<node *, size_t> & phaseMap) {
	typedef node_map::id_t id_t;
	std::map<id_t, size_t> numbering;
	std::vector<node *> nodeOrder;
	for (node_map::mapit i = m_nodeMap.begin(); i != m_nodeMap.end(); ++i) {
		numbering[i->second->get_id()] = nodeOrder.size();
		nodeOrder.push_back(i->second);
	}
	const size_t N = nodeOrder.size();

	tpie::disjoint_sets<size_t> unionFind(N);
	for (size_t i = 0; i < N; ++i)
		unionFind.make_set(i);

	const node_map::relmap_t & relations = m_nodeMap.get_relations();
	for (node_map::relmapit i = relations.begin(); i != relations.end(); ++i) {
		if (i->second.second != depends 
			&& i->second.second != no_forward_depends
			&& i->second.second != memory_share_depends)
			unionFind.union_set(numbering[i->first], numbering[i->second.first]);
	}

	const size_t NIL = N;
	std::vector<size_t> phaseNumber(N, NIL);
	size_t nextPhase = 0;
	for (size_t i = 0; i < N; ++i) {
		size_t & phase = phaseNumber[unionFind.find_set(i)];
		if (phase == NIL) {
			phase = nextPhase++;
		}
		phaseMap[nodeOrder[i]] = phase;
	}
}

void runtime::get_phase_graph(const std::map<node *, size_t> & phaseMap,
							  graph<size_t> & phaseGraph)
{
	for (std::map<node *, size_t>::const_iterator i = phaseMap.begin();
		 i != phaseMap.end(); ++i) {
		phaseGraph.add_node(i->second);
	}

	const node_map::relmap_t & relations = m_nodeMap.get_relations();
	for (node_map::relmapit i = relations.begin(); i != relations.end(); ++i) {
		if (i->second.second == depends 
			|| i->second.second == no_forward_depends
			|| i->second.second == memory_share_depends)
			phaseGraph.add_edge(phaseMap.find(m_nodeMap.get(i->second.first))->second,
								phaseMap.find(m_nodeMap.get(i->first))->second);
	}
}

// Compute the inverse of a permutation f : {0, 1, ... N-1} -> {0, 1, ... N-1}
/*static*/
std::vector<size_t> runtime::inverse_permutation(const std::vector<size_t> & f) {
	std::vector<size_t> result(f.size(), f.size());
	for (size_t i = 0; i < f.size(); ++i) {
		if (f[i] >= f.size())
			throw tpie::exception("inverse_permutation: f has bad range");
		if (result[f[i]] != f.size())
			throw tpie::exception("inverse_permutation: f is not injective");
		result[f[i]] = i;
	}
	for (size_t i = 0; i < result.size(); ++i) {
		if (result[i] == f.size())
			throw tpie::exception("inverse_permutation: f is not surjective");
	}
	return result;
}

void runtime::get_phases(const std::map<node *, size_t> & phaseMap,
						 const graph<size_t> & phaseGraph,
						 std::unordered_set<node_map::id_t> & evacuateWhenDone,
						 std::vector<std::vector<node *> > & phases)
{
	/*
	 * We have a dependency edge saying that a node in one phase shares memory with a node in another phase.
	 * If these two phases are not executed consecutively the shared memory will have to be evacuated to disk,
	 * since some other phase running between the two phases could need the memory.
	 * Obviously we want to minimize the number of evacuations, but how?
	 *
	 * Let a normal dependency between two phase be represented by a black edge
	 * and let a memory sharing dependency be represented by a red edge if the memory can be evacuated
	 * and green if it cannot be evacuated.
	 * We say that a non-black edge is satisfied if its two end points are consecutive in the topological order,
	 * so the objective is to maximize the number of satisfied edges.
	 * Also we must satisfy ALL green edges, if this is not possible the input is malformed,
	 * and someone has to implement an evacuate method somewhere.
	 *
	 * First note that a non-black edges cannot be satisfied
	 * if there exists an alternative path from its source to its destination (with length at least 2),
	 * so any such red edge can be recolored to black, if there is any such green edge the input is invalid.
	 *
	 * Next note that for any node we can satisfy at most one outgoing edge, and at most one incoming edge.
	 */

	std::vector<std::pair<size_t, size_t>> blackEdges;
	std::vector<std::pair<size_t, size_t>> redEdges;
	std::unordered_map<size_t, size_t> greenEdges;
	std::unordered_map<size_t, size_t> revGreenEdges;

	const node_map::relmap_t & relations = m_nodeMap.find_authority()->get_relations();
	for (node_map::relmapit i = relations.begin(); i != relations.end(); ++i) {
		// from and to is swapped in the relationship so that
		// to depends on from, meaning from should be run before to
		node *from = m_nodeMap.get(i->second.first);
		node *to = m_nodeMap.get(i->first);

		size_t fromPhase = phaseMap.find(from)->second;
		size_t toPhase = phaseMap.find(to)->second;
		bits::node_relation rel = i->second.second;

		if (fromPhase == toPhase) {
			// Not an edge between two different phases
			continue;
		}

		if (rel != memory_share_depends) {
			// Black edge
			log_pipe_debug() << "Black edge: " << fromPhase << " -> " << toPhase << std::endl;
			blackEdges.push_back({fromPhase, toPhase});
			continue;
		}

		if (from->can_evacuate()) {
			// Red edge
			log_pipe_debug() << "Red edge: " << fromPhase << " -> " << toPhase << std::endl;
			redEdges.push_back({fromPhase, toPhase});
		} else {
			// Green edge
			log_pipe_debug() << "Green edge: " << fromPhase << " -> " << toPhase << std::endl;

			// Check if we already have a green edge from fromPhase or to toPhase
			// If so one of edges can't be satisfied, but all green edges must be satisfied
			if (greenEdges.find(fromPhase) != greenEdges.end() ||
				revGreenEdges.find(toPhase) != revGreenEdges.end()) {
				throw tpie::exception("get_phases: can't satisfy all green edges");
			}
			greenEdges[fromPhase] = toPhase;
			revGreenEdges[toPhase] = fromPhase;
		}
	}

	disjoint_sets<size_t> contractedNodes(phaseGraph.size());
	for (size_t i : phaseGraph.get_node_set()) {
		contractedNodes.make_set(i);
	}

	for (const auto & p : greenEdges) {
		contractedNodes.union_set(p.first, p.second);
	}

	std::unordered_map<size_t, graph<size_t>> greenPaths;
	for (const auto & p : greenEdges) {
		size_t i = contractedNodes.find_set(p.first);
		greenPaths[i].add_edge(p.first, p.second);
	}

	satisfiable_graph contractedGraph;
	for (size_t i : phaseGraph.get_node_set()) {
		contractedGraph.add_node(contractedNodes.find_set(i));
	}

	/*
	 * Greedily prefer red edges over black in the topological order.
	 * First we add all black edges to the graph then all the red.
	 * If there is both a black edge and red edge between the same contracted node,
	 * we shall consider the edge as a red edge.
	 * This ensure that dfs in the topological order implementation
	 * will visit red edges later than black edges.
	 */
	std::set<std::pair<size_t, size_t>> redEdgeSet;
	for (auto p : redEdges) {
		p.first = contractedNodes.find_set(p.first);
		p.second = contractedNodes.find_set(p.second);
		if (p.first == p.second) continue;
		redEdgeSet.insert(p);
	}

	std::set<std::pair<size_t, size_t>> blackEdgeSet;
	for (auto p : blackEdges) {
		p.first = contractedNodes.find_set(p.first);
		p.second = contractedNodes.find_set(p.second);
		if (p.first == p.second) continue;
		if (!redEdgeSet.count(p)) {
			blackEdgeSet.insert(p);
		}
	}

	for (const auto * edges : {&blackEdgeSet, &redEdgeSet}) {
		for (const auto & p : *edges) {
			/*
			 * If we have an edge from one contracted node to another
			 * it must either be a green edge or an edge going
			 * in the same direction as the green path, because the graph is a DAG.
			 * So if we find a topological order for new the graph,
			 * the topological order without contractions will also satisfy this edge.
			 */
			contractedGraph.add_edge(p.first, p.second, edges == &redEdgeSet);
		}
	}

	std::vector<size_t> topologicalOrder;
	try {
		contractedGraph.topological_order(topologicalOrder);
	} catch (const not_a_dag_exception &) {
		throw tpie::exception("get_phases: can't satisfy all green edges");
	}

	// Expand contracted edges in topologicalOrder
	for (const auto & p : greenPaths) {
		size_t i = p.first;
		const graph<size_t> & g = p.second;

		std::vector<size_t> path;
		g.topological_order(path);

		auto it = std::find(topologicalOrder.begin(), topologicalOrder.end(), i);
		*it = *path.rbegin();
		topologicalOrder.insert(it, path.begin(), path.end() - 1);
	}

	// topologicalOrder[0] is the first phase to run,
	// topologicalOrder[1] the next, and so on.

	// Compute inverse permutation such that
	// topoOrderMap[i] is the time at which we run phase i.
	std::vector<size_t> topoOrderMap = inverse_permutation(topologicalOrder);

	// Distribute nodes according to the topological order
	phases.resize(topologicalOrder.size());
	for (std::map<node *, size_t>::const_iterator i = phaseMap.begin();
		 i != phaseMap.end(); ++i)
	{
		phases[topoOrderMap[i->second]].push_back(i->first);
	}

	std::unordered_set<node_map::id_t> previousNodes;
	bits::node_map::ptr nodeMap = (phases.front().front())->get_node_map()->find_authority();
	for (const auto & phase : phases) {
		for (const auto node : phase) {
			const auto range = nodeMap->get_relations().equal_range(node->get_id());
			for (auto it = range.first ; it != range.second ; ++it) {
				if (it->second.second != memory_share_depends) continue;
				if (previousNodes.count(it->second.first) != 0) continue;
				evacuateWhenDone.emplace(it->second.first);
			}
		}
		previousNodes.clear();
		for (const auto node : phase) {
			previousNodes.emplace(node->get_id());
		}
	}
}

void runtime::get_item_flow_graphs(std::vector<std::vector<node *> > & phases,
								   std::vector<graph<node *> > & itemFlow)
{
	itemFlow.resize(phases.size());
	for (size_t i = 0; i < phases.size(); ++i)
		get_graph(phases[i], itemFlow[i], true);
}

void runtime::get_actor_graphs(std::vector<std::vector<node *> > & phases,
							   std::vector<graph<node *> > & actors)
{
	actors.resize(phases.size());
	for (size_t i = 0; i < phases.size(); ++i)
		get_graph(phases[i], actors[i], false);
}

void runtime::get_graph(std::vector<node *> & phase, graph<node *> & result,
						bool itemFlow)
{
	const node_map::relmap_t & relations = m_nodeMap.get_relations();
	typedef node_map::relmapit relmapit;
	for (size_t i = 0; i < phase.size(); ++i) {
		result.add_node(phase[i]);
		std::pair<relmapit, relmapit> edges =
			relations.equal_range(phase[i]->get_id());
		for (relmapit j = edges.first; j != edges.second; ++j) {
			node * u = m_nodeMap.get(j->first);
			node * v = m_nodeMap.get(j->second.first);
			if (j->second.second == depends) continue;
			if (j->second.second == no_forward_depends) continue;
			if (j->second.second == memory_share_depends) continue;
			if (itemFlow && j->second.second == pulls) std::swap(u, v);
			result.add_edge(u, v);
		}
	}
}

bool runtime::is_initiator(node * n) {
	node_map::id_t id = n->get_id();
	return m_nodeMap.in_degree(id, pushes) == 0
		&& m_nodeMap.in_degree(id, pulls) == 0;
}

bool runtime::has_initiator(const std::vector<node *> & phase) {
	for (size_t i = 0; i < phase.size(); ++i)
		if (is_initiator(phase[i])) return true;
	return false;
}

void runtime::ensure_initiators(const std::vector<std::vector<node *> > & phases) {
	for (size_t i = 0; i < phases.size(); ++i)
		if (!has_initiator(phases[i])) throw no_initiator_node();
}

void runtime::prepare_all(const std::vector<graph<node *> > & itemFlow) {
	for (size_t i = 0; i < itemFlow.size(); ++i) {
		const graph<node *> & g = itemFlow[i];
		std::vector<node *> topoOrder;
		g.topological_order(topoOrder);
		for (size_t i = 0; i < topoOrder.size(); ++i) {
			topoOrder[i]->set_state(node::STATE_IN_PREPARE);
			topoOrder[i]->prepare();
			topoOrder[i]->set_state(node::STATE_AFTER_PREPARE);
		}
	}
}

void runtime::evacuate_all(const std::vector<node *> & phase, 
						   const std::unordered_set<node_map::id_t> & evacuateWhenDone) {
	for (auto node : phase) {
		if (evacuateWhenDone.count(node->get_id()) == 0)
			continue;
		if (node->can_evacuate()) {
			node->evacuate();
			tpie::log_pipe_debug() << "Evacuated node " << node->get_id() << std::endl;
		} else {
			tpie::log_warning() << "Need to evacuate but not possible." << node->get_id() << std::endl;
		}
	}
}

void runtime::propagate_all(const graph<node *> & itemFlow) {
	std::vector<node *> topoOrder;
	itemFlow.topological_order(topoOrder);
	for (size_t i = 0; i < topoOrder.size(); ++i) {
		topoOrder[i]->set_state(node::STATE_IN_PROPAGATE);
		topoOrder[i]->propagate();
		topoOrder[i]->set_state(node::STATE_AFTER_PROPAGATE);
	}
}

void runtime::set_progress_indicators(const std::vector<node *> & phase,
									  progress_indicator_base & pi) {
	for (size_t i = 0; i < phase.size(); ++i)
		phase[i]->set_progress_indicator(&pi);
}

void runtime::go_initiators(const std::vector<node *> & phase) {
	std::vector<node *> initiators;
	for (size_t i = 0; i < phase.size(); ++i)
		if (is_initiator(phase[i])) initiators.push_back(phase[i]);
	for (size_t i = 0; i < initiators.size(); ++i) {
		initiators[i]->set_state(node::STATE_IN_GO);
		initiators[i]->go();
		initiators[i]->set_state(node::STATE_AFTER_BEGIN);
	}
}

/*static*/
void runtime::set_resource_being_assigned(const std::vector<node *> & nodes,
										  resource_type type) {
	for (node * n : nodes)
		n->set_resource_being_assigned(type);
}

/*static*/
void runtime::assign_files(const std::vector<std::vector<node *> > & phases,
							memory_size_type files) {
	for (size_t phase = 0; phase < phases.size(); ++phase) {
		file_runtime frt(phases[phase]);

		double c = get_files_factor(files, frt);
#ifndef TPIE_NDEBUG
		frt.print_usage(c, log_pipe_debug());
#endif // TPIE_NDEBUG
		set_resource_being_assigned(phases[phase], FILES);
		frt.assign_usage(c);
		set_resource_being_assigned(phases[phase], NO_RESOURCE);
	}
}

/*static*/
void runtime::reassign_files(const std::vector<std::vector<node *> > & phases,
							  size_t phase,
							  memory_size_type files) {
	file_runtime frt(phases[phase]);
	double c = get_files_factor(files, frt);
#ifndef TPIE_NDEBUG
	frt.print_usage(c, log_pipe_debug());
#endif // TPIE_NDEBUG
	set_resource_being_assigned(phases[phase], FILES);
	frt.assign_usage(c);
	set_resource_being_assigned(phases[phase], NO_RESOURCE);
}

/*static*/
double runtime::get_files_factor(memory_size_type files, const file_runtime & frt) {
	memory_size_type min = frt.sum_minimum_usage();
	if (min > files) {
		log_warning() << "Not enough files for pipelining phase ("
					  << min << " > " << files << ")"
					  << std::endl;
		return 0.0;
	}

	// This case is handled specially to avoid dividing by zero later on.
	double fraction_sum = frt.sum_fraction();
	if (fraction_sum < 1e-9) {
		return 0.0;
	}

	double c_lo = 0.0;
	double c_hi = 1.0;
	// Exponential search
	memory_size_type oldFilesAssigned = 0;
	while (true) {
		double factor = files * c_hi / fraction_sum;
		memory_size_type filesAssigned = frt.sum_assigned_usage(factor);
		if (filesAssigned < files && filesAssigned != oldFilesAssigned)
			c_hi *= 2;
		else
			break;
		oldFilesAssigned = filesAssigned;
	}

	// Binary search
	while (c_hi - c_lo > 1e-6) {
		double c = c_lo + (c_hi-c_lo)/2;
		double factor = files * c / fraction_sum;
		memory_size_type filesAssigned = frt.sum_assigned_usage(factor);

		if (filesAssigned > files) {
			c_hi = c;
		} else {
			c_lo = c;
		}
	}

	return files * c_lo / fraction_sum;
}

/*static*/
void runtime::assign_memory(const std::vector<std::vector<node *> > & phases,
							memory_size_type memory,
							datastructure_runtime & drt) {
	for (size_t phase = 0; phase < phases.size(); ++phase) {
		memory_runtime mrt(phases[phase]);

		double c = get_memory_factor(memory, phase, mrt, drt, false);
		drt.minimize_factor(c, phase);
	}

	for (size_t phase = 0; phase < phases.size(); ++phase) {
		memory_runtime mrt(phases[phase]);

		double c = get_memory_factor(memory, phase, mrt, drt, true);
#ifndef TPIE_NDEBUG
		mrt.print_usage(c, log_pipe_debug());
#endif // TPIE_NDEBUG
		set_resource_being_assigned(phases[phase], MEMORY);
		mrt.assign_usage(c);
		set_resource_being_assigned(phases[phase], NO_RESOURCE);
	}
	drt.assign_memory();
}

/*static*/
void runtime::reassign_memory(const std::vector<std::vector<node *> > & phases,
							  size_t phase,
							  memory_size_type memory,
							  const datastructure_runtime & drt) {
	memory_runtime mrt(phases[phase]);
	double c = get_memory_factor(memory, phase, mrt, drt, true);
#ifndef TPIE_NDEBUG
	mrt.print_usage(c, log_pipe_debug());
#endif // TPIE_NDEBUG
	set_resource_being_assigned(phases[phase], MEMORY);
	mrt.assign_usage(c);
	set_resource_being_assigned(phases[phase], NO_RESOURCE);
}

/*static*/
double runtime::get_memory_factor(memory_size_type memory, memory_size_type phase, const memory_runtime & mrt, const datastructure_runtime & drt, bool datastructures_locked) {
	memory_size_type min = mrt.sum_minimum_usage() + drt.sum_minimum_memory(phase);
	if (min > memory) {
		log_warning() << "Not enough memory for pipelining phase ("
					  << min << " > " << memory << ")"
					  << std::endl;
		return 0.0;
	}

	// This case is handled specially to avoid dividing by zero later on.
	double fraction_sum = mrt.sum_fraction() + drt.sum_fraction(phase);
	if (fraction_sum < 1e-9) {
		return 0.0;
	}

	double c_lo = 0.0;
	double c_hi = 1.0;
	// Exponential search
	memory_size_type oldMemoryAssigned = 0;
	while (true) {
		double factor = memory * c_hi / fraction_sum;
		memory_size_type memoryAssigned = mrt.sum_assigned_usage(factor) + (datastructures_locked ? drt.sum_assigned_memory(phase) : drt.sum_assigned_memory(factor, phase));
		if (memoryAssigned < memory && memoryAssigned != oldMemoryAssigned)
			c_hi *= 2;
		else
			break;
		oldMemoryAssigned = memoryAssigned;
	}

	// Binary search
	while (c_hi - c_lo > 1e-6) {
		double c = c_lo + (c_hi-c_lo)/2;
		double factor = memory * c / fraction_sum;
		memory_size_type memoryAssigned = mrt.sum_assigned_usage(factor) + (datastructures_locked ? drt.sum_assigned_memory(phase) : drt.sum_assigned_memory(factor, phase));

		if (memoryAssigned > memory) {
			c_hi = c;
		} else {
			c_lo = c;
		}
	}

	return memory * c_lo / fraction_sum;
}

}

}

}
