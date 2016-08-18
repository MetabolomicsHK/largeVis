// [[Rcpp::plugins(openmp)]]
// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::depends(RcppArmadillo)]]
// [[Rcpp::depends(RcppProgress)]]
#include "pq.h"

class HDBSCAN : UF<long long> {
public:
  class CompareDist {
  public:
    bool operator()(iddist n1, iddist n2) {
      return n1.second > n2.second;
    }
  };
  typedef std::priority_queue<iddist, 
                              std::vector<iddist>, 
                              CompareDist> DistanceSorter;
  
  long long starterIndex;
  arma::sp_mat mrdMatrix;
  
  long long*   minimum_spanning_tree;
  double*      minimum_spanning_distances;
  
  HDBSCAN(const int N,
          const long long nedges,
          bool verbose) : UF(N, nedges, verbose) {
            minimum_spanning_tree = new long long[N];
            minimum_spanning_distances = new double[N];
          }
  
  arma::vec makeCoreDistances(const arma::sp_mat& edges, const int K) {
    arma::vec coreDistances = arma::vec(N);
    for (long long n = 0; n < N && p.increment() ; n++) {
      DistanceSorter srtr = DistanceSorter();
      for (auto it = edges.begin_col(n);
           it != edges.end_col(n);
           it++) srtr.emplace(iddist(it.row(), *it));
      for (int k = 0; k != K && srtr.size() > 1; k++) srtr.pop();
      coreDistances[n] = srtr.top().second;
    }
    return coreDistances;
  }
  
  void makeMRD(const arma::sp_mat& edges, const int K) {
    mrdMatrix = arma::sp_mat(edges);
    arma::vec coreDistances = makeCoreDistances(edges, K);
    double bestOverall = INFINITY;
    for (auto it = mrdMatrix.begin(); 
         it != mrdMatrix.end();
         it++) if (p.increment()) {
      long long i = it.row();
      long long j = it.col();
      double d = max(coreDistances[i], coreDistances[j]);
      d = (d > *it) ? d : *it;
      if (d < bestOverall) {
        starterIndex = j;
        bestOverall = d;
      }
      *it = d;
    }
  }

  void primsAlgorithm() {
    double* Cv = minimum_spanning_distances;
    long long* Ev = minimum_spanning_tree;
    MinIndexedPQ<long long, double> Q = MinIndexedPQ<long long, double>(N);
    for (long long n = 0; n != N; n++) {
      Cv[n] = (n == starterIndex) ? -1 : INFINITY;
      Ev[n] = -1;
      Q.insert(n, Cv[n]);
    }
    
    long long v;
    while (! Q.isEmpty() && p.increment()) {
      v = Q.deleteMin();
      for (auto it = mrdMatrix.begin_row(v);
           it != mrdMatrix.end_row(v);
           it++) {
        long long w = it.col();
        if (Q.contains(w) && *it < Cv[w]) {
          Q.changeKey(w, *it);
          Cv[w] = *it;
          Ev[w] = v;
        }
      }
    }
  }

  void buildHierarchy() {
    setup();
    DistanceSorter srtr = DistanceSorter();
    for (long long n = 0; n != N && p.increment(); n++) {
      double distance = minimum_spanning_distances[n];
      srtr.push(iddist(n, distance));
    }

    while (! srtr.empty() && p.increment()) {
      const iddist ijd = srtr.top();
      srtr.pop();
      agglomerate(ijd.first, minimum_spanning_tree[ijd.first], ijd.second);
    }
  }
  
  arma::mat process(const arma::sp_mat& edges, const int K, const int minPts) {
    makeMRD(edges, K);
    primsAlgorithm();
    buildHierarchy();
    condense(minPts);
    determineStability(minPts);
    extractClusters();
    return getClusters();
  }
  
  
  Rcpp::List reportHierarchy() {
    long long survivingClusterCnt = survivingClusters.size();
    IntegerVector parent = IntegerVector(survivingClusterCnt);
    IntegerVector nodeMembership = IntegerVector(N);
    NumericVector stabilities = NumericVector(survivingClusterCnt);
    IntegerVector selected = IntegerVector(survivingClusterCnt);
    
    for (typename std::set< long long >::iterator it = roots.begin();
         it != roots.end();
         it++) reportAHierarchy(*it, *it, parent, nodeMembership, 
         stabilities, selected, 0, 0);
    
    NumericVector lambdas = NumericVector(N);
    // FIXME - need to adjust this to be lambda_p not birth
    for (long long n = 0; n != N; n++) lambdas[n] = lambda_births[n];

    return Rcpp::List::create(Rcpp::Named("nodemembership") = nodeMembership,
                              Rcpp::Named("lambda") = lambdas, 
                              Rcpp::Named("parent") = parent, 
                              Rcpp::Named("stability") = stabilities,
                              Rcpp::Named("selected") = selected);
  }
};

// [[Rcpp::export]]
List hdbscanc(const arma::sp_mat& edges, const int K, const int minPts, const bool verbose) {
  HDBSCAN object = HDBSCAN(edges.n_cols, edges.n_elem, verbose);
  arma::mat clusters = object.process(edges, K, minPts);
  arma::ivec tree = arma::ivec(edges.n_cols);
  for (int n = 0; n != edges.n_cols; n++) {
    tree[n] = object.minimum_spanning_tree[n];
  }
  Rcpp::List hierarchy = object.reportHierarchy();
  return Rcpp::List::create(Rcpp::Named("clusters") = clusters,
                            Rcpp::Named("tree") = tree,
                            Rcpp::Named("hierarchy") = hierarchy);
}