#pragma once

#include "base/assert.hpp"
#include "base/cancellable.hpp"
#include "base/logging.hpp"
#include "routing/base/graph.hpp"
#include "std/algorithm.hpp"
#include "std/functional.hpp"
#include "std/iostream.hpp"
#include "std/map.hpp"
#include "std/queue.hpp"
#include "std/vector.hpp"

namespace routing
{
template <typename TGraph>
class AStarAlgorithm : public my::Cancellable
{
public:
  using TGraphType = TGraph;
  using TVertexType = typename TGraphType::TVertexType;
  using TEdgeType = typename TGraphType::TEdgeType;

  static uint32_t const kCancelledPollPeriod;
  static uint32_t const kQueueSwtichPeriod;
  static double const kEpsilon;

  enum class Result
  {
    OK,
    NoPath,
    Cancelled
  };

  friend ostream & operator<<(ostream & os, Result const & result)
  {
    switch (result)
    {
      case Result::OK:
        os << "OK";
        break;
      case Result::NoPath:
        os << "NoPath";
        break;
      case Result::Cancelled:
        os << "Cancelled";
        break;
    }
    return os;
  }

  AStarAlgorithm() : m_graph(nullptr) {}

  Result FindPath(vector<TVertexType> const & startPos, vector<TVertexType> const & finalPos,
                  vector<TVertexType> & path) const;
  Result FindPathBidirectional(vector<TVertexType> const & startPos,
                               vector<TVertexType> const & finalPos,
                               vector<TVertexType> & path) const;

  void SetGraph(TGraph const & graph) { m_graph = &graph; }

private:
  // State is what is going to be put in the priority queue. See the
  // comment for FindPath for more information.
  struct State
  {
    State(TVertexType const & vertex, double distance) : vertex(vertex), distance(distance) {}

    inline bool operator>(State const & rhs) const { return distance > rhs.distance; }

    TVertexType vertex;
    double distance;
  };

  // BidirectionalStepContext keeps all the information that is needed to
  // search starting from one of the two directions. Its main
  // purpose is to make the code that changes directions more readable.
  struct BidirectionalStepContext
  {
    BidirectionalStepContext(bool forward, vector<TVertexType> const & startPos,
                             vector<TVertexType> const & finalPos, TGraphType const & graph)
        : forward(forward), startPos(startPos), finalPos(finalPos), graph(graph)
    {
      bestVertex = forward ? startPos[0] : finalPos[0];
      pS = ConsistentHeuristic(startPos[0]);
    }

    double TopDistance() const
    {
      ASSERT(!queue.empty(), ());
      return bestDistance.at(queue.top().vertex);
    }

    // p_f(v) = 0.5*(π_f(v) - π_r(v)) + 0.5*π_r(t)
    // p_r(v) = 0.5*(π_r(v) - π_f(v)) + 0.5*π_f(s)
    // p_r(v) + p_f(v) = const. Note: this condition is called consistence.
    double ConsistentHeuristic(TVertexType const & v) const
    {
      double piF = graph.HeuristicCostEstimate(v, finalPos[0]);
      double piR = graph.HeuristicCostEstimate(v, startPos[0]);
      double const piRT = graph.HeuristicCostEstimate(finalPos[0], startPos[0]);
      double const piFS = graph.HeuristicCostEstimate(startPos[0], finalPos[0]);
      if (forward)
      {
        /// @todo careful: with this "return" here and below in the Backward case
        /// the heuristic becomes inconsistent but still seems to work.
        /// return HeuristicCostEstimate(v.pos, finalPos);
        return 0.5 * (piF - piR + piRT);
      }
      else
      {
        // return HeuristicCostEstimate(v.pos, startPos);
        return 0.5 * (piR - piF + piFS);
      }
    }

    bool const forward;
    vector<TVertexType> const & startPos;
    vector<TVertexType> const & finalPos;
    TGraph const & graph;

    priority_queue<State, vector<State>, greater<State>> queue;
    map<TVertexType, double> bestDistance;
    map<TVertexType, TVertexType> parent;
    TVertexType bestVertex;

    double pS;
  };

  static void ReconstructPath(TVertexType const & v, map<TVertexType, TVertexType> const & parent,
                              vector<TVertexType> & path);
  static void ReconstructPathBidirectional(TVertexType const & v, TVertexType const & w,
                                           map<TVertexType, TVertexType> const & parentV,
                                           map<TVertexType, TVertexType> const & parentW,
                                           vector<TVertexType> & path);

  TGraphType const * m_graph;
};

// static
template <typename TGraph>
uint32_t const AStarAlgorithm<TGraph>::kCancelledPollPeriod = 128;

// static
template <typename TGraph>
uint32_t const AStarAlgorithm<TGraph>::kQueueSwtichPeriod = 128;

// static
template <typename TGraph>
double const AStarAlgorithm<TGraph>::kEpsilon = 1e-6;

// This implementation is based on the view that the A* algorithm
// is equivalent to Dijkstra's algorithm that is run on a reweighted
// version of the graph. If an edge (v, w) has length l(v, w), its reduced
// cost is l_r(v, w) = l(v, w) + pi(w) - pi(v), where pi() is any function
// that ensures l_r(v, w) >= 0 for every edge. We set pi() to calculate
// the shortest possible distance to a goal node, and this is a common
// heuristic that people use in A*.
// Refer to this paper for more information:
// http://research.microsoft.com/pubs/154937/soda05.pdf
//
// The vertices of the graph are of type RoadPos.
// The edges of the graph are of type PossibleTurn.
template <typename TGraph>
typename AStarAlgorithm<TGraph>::Result AStarAlgorithm<TGraph>::FindPath(
    vector<TVertexType> const & startPos, vector<TVertexType> const & finalPos,
    vector<TVertexType> & path) const
{
  ASSERT(m_graph, ());
  ASSERT(!startPos.empty(), ());
  ASSERT(!finalPos.empty(), ());

  vector<TVertexType> sortedStartPos(startPos.begin(), startPos.end());
  sort(sortedStartPos.begin(), sortedStartPos.end());

  map<TVertexType, double> bestDistance;
  priority_queue<State, vector<State>, greater<State>> queue;
  map<TVertexType, TVertexType> parent;
  for (auto const & rp : finalPos)
  {
    VERIFY(bestDistance.emplace(rp, 0.0).second, ());
    queue.push(State(rp, 0.0));
  }

  uint32_t steps = 0;

  while (!queue.empty())
  {
    ++steps;
    if (steps % kCancelledPollPeriod == 0 && IsCancelled())
      return Result::Cancelled;

    State const stateV = queue.top();
    queue.pop();

    if (stateV.distance > bestDistance[stateV.vertex])
      continue;

    if (binary_search(sortedStartPos.begin(), sortedStartPos.end(), stateV.vertex))
    {
      ReconstructPath(stateV.vertex, parent, path);
      return Result::OK;
    }

    vector<TEdgeType> adj;
    m_graph->GetAdjacencyList(stateV.vertex, adj);
    for (auto const & edge : adj)
    {
      State stateW(edge.GetTarget(), 0.0);
      if (stateV.vertex == stateW.vertex)
        continue;
      double const len = edge.GetWeight();
      double const piV = m_graph->HeuristicCostEstimate(stateV.vertex, sortedStartPos[0]);
      double const piW = m_graph->HeuristicCostEstimate(stateW.vertex, sortedStartPos[0]);
      double const reducedLen = len + piW - piV;

      CHECK(reducedLen >= -kEpsilon, ("Invariant violated:", reducedLen, "<", -kEpsilon));
      double const newReducedDist = stateV.distance + max(reducedLen, 0.0);

      auto t = bestDistance.find(stateW.vertex);
      if (t != bestDistance.end() && newReducedDist >= t->second - kEpsilon)
        continue;

      stateW.distance = newReducedDist;
      bestDistance[stateW.vertex] = newReducedDist;
      parent[stateW.vertex] = stateV.vertex;
      queue.push(stateW);
    }
  }

  return Result::NoPath;
}

/// @todo This may work incorrectly if (startPos.size() > 1) or (finalPos.size() > 1).
template <typename TGraph>
typename AStarAlgorithm<TGraph>::Result AStarAlgorithm<TGraph>::FindPathBidirectional(
    vector<TVertexType> const & startPos, vector<TVertexType> const & finalPos,
    vector<TVertexType> & path) const
{
  ASSERT(!startPos.empty(), ());
  ASSERT(!finalPos.empty(), ());

  BidirectionalStepContext forward(true /* forward */, startPos, finalPos, *m_graph);
  BidirectionalStepContext backward(false /* forward */, startPos, finalPos, *m_graph);

  bool foundAnyPath = false;
  double bestPathLength = 0.0;

  for (auto const & rp : startPos)
  {
    VERIFY(forward.bestDistance.emplace(rp, 0.0).second, ());
    forward.queue.push(State(rp, 0.0 /* distance */));
  }
  for (auto const & rp : finalPos)
  {
    VERIFY(backward.bestDistance.emplace(rp, 0.0).second, ());
    backward.queue.push(State(rp, 0.0 /* distance */));
  }

  // To use the search code both for backward and forward directions
  // we keep the pointers to everything related to the search in the
  // 'current' and 'next' directions. Swapping these pointers indicates
  // changing the end we are searching from.
  BidirectionalStepContext * cur = &forward;
  BidirectionalStepContext * nxt = &backward;

  // It is not necessary to check emptiness for both queues here
  // because if we have not found a path by the time one of the
  // queues is exhausted, we never will.
  uint32_t steps = 0;
  while (!cur->queue.empty() && !nxt->queue.empty())
  {
    ++steps;

    if (steps % kCancelledPollPeriod == 0 && IsCancelled())
      return Result::Cancelled;

    if (steps % kQueueSwtichPeriod == 0)
      swap(cur, nxt);

    double const curTop = cur->TopDistance();
    double const nxtTop = nxt->TopDistance();
    double const pTopV = cur->ConsistentHeuristic(cur->queue.top().vertex);
    double const pTopW = nxt->ConsistentHeuristic(nxt->queue.top().vertex);

    // The intuition behind this is that we cannot obtain a path shorter
    // than the left side of the inequality because that is how any path we find
    // will look like (see comment for curPathLength below).
    // We do not yet have the proof that we will not miss a good path by doing so.
    if (foundAnyPath &&
        curTop + nxtTop - pTopV + cur->pS - pTopW + nxt->pS >= bestPathLength - kEpsilon)
    {
      ReconstructPathBidirectional(cur->bestVertex, nxt->bestVertex, cur->parent, nxt->parent,
                                   path);
      CHECK(!path.empty(), ());
      if (!cur->forward)
        reverse(path.begin(), path.end());
      return Result::OK;
    }

    State const stateV = cur->queue.top();
    cur->queue.pop();

    if (stateV.distance > cur->bestDistance[stateV.vertex])
      continue;

    vector<TEdgeType> adj;
    m_graph->GetAdjacencyList(stateV.vertex, adj);
    for (auto const & edge : adj)
    {
      State stateW(edge.GetTarget(), 0.0);
      if (stateV.vertex == stateW.vertex)
        continue;
      double const len = edge.GetWeight();
      double const pV = cur->ConsistentHeuristic(stateV.vertex);
      double const pW = cur->ConsistentHeuristic(stateW.vertex);
      double const reducedLen = len + pW - pV;
      double const pRW = nxt->ConsistentHeuristic(stateW.vertex);
      CHECK(reducedLen >= -kEpsilon, ("Invariant violated:", reducedLen, "<", -kEpsilon));
      double newReducedDist = stateV.distance + max(reducedLen, 0.0);

      typename map<TVertexType, double>::const_iterator t = cur->bestDistance.find(stateW.vertex);
      if (t != cur->bestDistance.end() && newReducedDist >= t->second - kEpsilon)
        continue;

      if (nxt->bestDistance.find(stateW.vertex) != nxt->bestDistance.end())
      {
        double const distW = nxt->bestDistance[stateW.vertex];
        // Length that the path we've just found has in the original graph:
        // find the length of the path's parts in the reduced forward and backward
        // graphs and remove the heuristic adjustments.
        double const curPathLength = newReducedDist + distW - pW + cur->pS - pRW + nxt->pS;
        // No epsilon here: it is ok to overshoot slightly.
        if (!foundAnyPath || bestPathLength > curPathLength)
        {
          bestPathLength = curPathLength;
          foundAnyPath = true;
          cur->bestVertex = stateV.vertex;
          nxt->bestVertex = stateW.vertex;
        }
      }

      stateW.distance = newReducedDist;
      cur->bestDistance[stateW.vertex] = newReducedDist;
      cur->parent[stateW.vertex] = stateV.vertex;
      cur->queue.push(stateW);
    }
  }

  return Result::NoPath;
}

// static
template <typename TGraph>
void AStarAlgorithm<TGraph>::ReconstructPath(TVertexType const & v,
                                             map<TVertexType, TVertexType> const & parent,
                                             vector<TVertexType> & path)
{
  path.clear();
  TVertexType cur = v;
  while (true)
  {
    path.push_back(cur);
    auto it = parent.find(cur);
    if (it == parent.end())
      break;
    cur = it->second;
  }
}

// static
template <typename TGraph>
void AStarAlgorithm<TGraph>::ReconstructPathBidirectional(
    TVertexType const & v, TVertexType const & w, map<TVertexType, TVertexType> const & parentV,
    map<TVertexType, TVertexType> const & parentW, vector<TVertexType> & path)
{
  vector<TVertexType> pathV;
  ReconstructPath(v, parentV, pathV);
  vector<TVertexType> pathW;
  ReconstructPath(w, parentW, pathW);
  path.insert(path.end(), pathV.rbegin(), pathV.rend());
  path.insert(path.end(), pathW.begin(), pathW.end());
}
}  // namespace routing
