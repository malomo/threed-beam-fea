// Copyright 2015. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: ryan.latture@gmail.com (Ryan Latture)

#include <Eigen/LU>
#include <boost/format.hpp>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

#include "threed_beam_fea.h"

namespace fea {

namespace {
void writeStringToTxt(std::string filename, std::string data) {
  std::ofstream output_file;
  output_file.open(filename);

  if (!output_file.is_open()) {
    throw std::runtime_error(
        (boost::format("Error opening file %s.") % filename).str());
  }
  output_file << data;
  output_file.close();
}
} // namespace

inline double norm(const Node &n1, const Node &n2) {
  const Node dn = n2 - n1;
  return dn.norm();
}

void GlobalStiffAssembler::calcKelem(unsigned int i, const Job &job) {
  // extract element properties
  const double EA = job.props[i].EA;   // Young's modulus * cross area
  const double EIz = job.props[i].EIz; // Young's modulus* I3
  const double EIy = job.props[i].EIy; // Young's modulus* I2
  const double GJ = job.props[i].GJ;

  // store node indices of current element
  const int nn1 = job.elems[i][0];
  const int nn2 = job.elems[i][1];

  // calculate the length of the element
  const double length = norm(job.nodes[nn1], job.nodes[nn2]);

  // store the entries in the (local) elemental stiffness matrix as temporary
  // values to avoid recalculation
  const double tmpEA = EA / length;
  const double tmpGJ = GJ / length;

  const double tmp12z = 12.0 * EIz / (length * length * length); // ==k3
  const double tmp6z = 6.0 * EIz / (length * length);
  const double tmp1z = EIz / length;

  const double tmp12y = 12.0 * EIy / (length * length * length);
  const double tmp6y = 6.0 * EIy / (length * length);
  const double tmp1y = EIy / length;

  // update local elemental stiffness matrix
  Klocal(0, 0) = tmpEA;
  Klocal(0, 6) = -tmpEA;
  Klocal(1, 1) = tmp12z;
  Klocal(1, 5) = tmp6z;
  Klocal(1, 7) = -tmp12z;
  Klocal(1, 11) = tmp6z;
  Klocal(2, 2) = tmp12y;
  Klocal(2, 4) = -tmp6y;
  Klocal(2, 8) = -tmp12y;
  Klocal(2, 10) = -tmp6y;
  Klocal(3, 3) = tmpGJ;
  Klocal(3, 9) = -tmpGJ;
  Klocal(4, 2) = -tmp6y;
  Klocal(4, 4) = 4.0 * tmp1y;
  Klocal(4, 8) = tmp6y;
  Klocal(4, 10) = 2.0 * tmp1y;
  Klocal(5, 1) = tmp6z;
  Klocal(5, 5) = 4.0 * tmp1z;
  Klocal(5, 7) = -tmp6z;
  Klocal(5, 11) = 2.0 * tmp1z;
  Klocal(6, 0) = -tmpEA;
  Klocal(6, 6) = tmpEA;
  Klocal(7, 1) = -tmp12z;
  Klocal(7, 5) = -tmp6z;
  Klocal(7, 7) = tmp12z;
  Klocal(7, 11) = -tmp6z;
  Klocal(8, 2) = -tmp12y;
  Klocal(8, 4) = tmp6y;
  Klocal(8, 8) = tmp12y;
  Klocal(8, 10) = tmp6y;
  Klocal(9, 3) = -tmpGJ;
  Klocal(9, 9) = tmpGJ;
  Klocal(10, 2) = -tmp6y;
  Klocal(10, 4) = 2.0 * tmp1y;
  Klocal(10, 8) = tmp6y;
  Klocal(10, 10) = 4.0 * tmp1y;
  Klocal(11, 1) = tmp6z;
  Klocal(11, 5) = 2.0 * tmp1z;
  Klocal(11, 7) = -tmp6z;
  Klocal(11, 11) = 4.0 * tmp1z;

  // calculate unit normal vector along local x-direction
  const Eigen::Vector3d nx = (job.nodes[nn2] - job.nodes[nn1]).normalized();
  // calculate unit normal vector along y-direction
  const Eigen::Vector3d ny = job.props[i].normal_vec.normalized();
  // calculate the unit normal vector in local z direction
  const Eigen::Vector3d nz = nx.cross(ny).normalized();
  RotationMatrix r;
  r.row(0) = nx;
  r.row(1) = ny;
  r.row(2) = nz;
  // update rotation matrices
  calcAelem(r);
  AelemT = Aelem.transpose();

  std::ofstream KlocalFile("Klocal.csv");
  if (KlocalFile.is_open()) {
    const static Eigen::IOFormat CSVFormat(Eigen::StreamPrecision,
                                           Eigen::DontAlignCols, ", ", "\n");
    KlocalFile << Klocal.format(CSVFormat) << '\n';
    KlocalFile.close();
  }
  // update Kelem
  Kelem = AelemT * Klocal * Aelem;
  //  std::ofstream KelemFile("Kelem.csv");
  //  if (KelemFile.is_open()) {
  //    const static Eigen::IOFormat CSVFormat(Eigen::StreamPrecision,
  //                                           Eigen::DontAlignCols, ", ",
  //                                           "\n");
  //    KelemFile << Kelem.format(CSVFormat) << '\n';
  //    KelemFile.close();
  //  }

  LocalMatrix KlocalAelem;
  KlocalAelem = Klocal * Aelem;
  perElemKlocalAelem[i] = KlocalAelem;
};

void GlobalStiffAssembler::calcAelem(const RotationMatrix &r) {
  // update rotation matrix
  Aelem.block<3, 3>(0, 0) = r;
  Aelem.block<3, 3>(3, 3) = r;
  Aelem.block<3, 3>(6, 6) = r;
  Aelem.block<3, 3>(9, 9) = r;
}

std::vector<LocalMatrix> GlobalStiffAssembler::getPerElemKlocalAelem() const {
  return perElemKlocalAelem;
};

void GlobalStiffAssembler::operator()(SparseMat &Kg, const Job &job,
                                      const std::vector<Tie> &ties) {
  int nn1, nn2;
  unsigned int row, col;
  const unsigned int dofs_per_elem = DOF::NUM_DOFS;

  // form vector to hold triplets that will be used to assemble global stiffness
  // matrix
  std::vector<Eigen::Triplet<double>> triplets;
  triplets.reserve(40 * job.elems.size() + 4 * dofs_per_elem * ties.size());

  perElemKlocalAelem.resize(job.elems.size());
  for (unsigned int i = 0; i < job.elems.size(); ++i) {
    // update Kelem with current elemental stiffness matrix
    calcKelem(i, job); // 12x12 matrix

    // get sparse representation of the current elemental stiffness matrix
    SparseKelem = Kelem.sparseView();

    // pull out current node indices
    nn1 = job.elems[i][0];
    nn2 = job.elems[i][1];

    for (unsigned int j = 0; j < SparseKelem.outerSize(); ++j) {
      for (SparseMat::InnerIterator it(SparseKelem, j); it; ++it) {
        row = it.row();
        col = it.col();

        // check position in local matrix and update corresponding global
        // position
        if (row < 6) {
          // top left
          if (col < 6) {
            triplets.push_back(Eigen::Triplet<double>(dofs_per_elem * nn1 + row,
                                                      dofs_per_elem * nn1 + col,
                                                      it.value()));
          }
          // top right
          else {
            triplets.push_back(Eigen::Triplet<double>(
                dofs_per_elem * nn1 + row, dofs_per_elem * (nn2 - 1) + col,
                it.value())); // I: Giati nn2-1 kai oxi nn2?
          }
        } else {
          // bottom left
          if (col < 6) {
            triplets.push_back(Eigen::Triplet<double>(
                dofs_per_elem * (nn2 - 1) + row, dofs_per_elem * nn1 + col,
                it.value())); // I: Giati nn2-1 kai oxi nn2? Epeidi ta nn2 ston
                              // Kelem exoun idi offset 6
          }
          // bottom right
          else {
            triplets.push_back(Eigen::Triplet<double>(
                dofs_per_elem * (nn2 - 1) + row,
                dofs_per_elem * (nn2 - 1) + col, it.value()));
          }
        }
      }
    }
  }

  loadTies(triplets, ties);

  Kg.setFromTriplets(triplets.begin(), triplets.end());
};

void loadBCs(SparseMat &Kg, SparseMat &force_vec, const std::vector<BC> &BCs,
             unsigned int num_nodes) {
  unsigned int bc_idx;
  const unsigned int dofs_per_elem = DOF::NUM_DOFS;
  // calculate the index that marks beginning of Lagrange multiplier
  // coefficients
  const unsigned int global_add_idx = dofs_per_elem * num_nodes;

  for (size_t i = 0; i < BCs.size(); ++i) {
    bc_idx = dofs_per_elem * BCs[i].node + BCs[i].dof;

    // update global stiffness matrix
    Kg.insert(bc_idx, global_add_idx + i) = 1;
    Kg.insert(global_add_idx + i, bc_idx) = 1;

    // update force vector. All values are already zero. Only update if BC if
    // non-zero.
    if (std::abs(BCs[i].value) > std::numeric_limits<double>::epsilon()) {
      force_vec.insert(global_add_idx + i, 0) = BCs[i].value;
    }
  }
};

void loadEquations(SparseMat &Kg, const std::vector<Equation> &equations,
                   unsigned int num_nodes, unsigned int num_bcs) {
  size_t row_idx, col_idx;
  const unsigned int dofs_per_elem = DOF::NUM_DOFS;
  const unsigned int global_add_idx = dofs_per_elem * num_nodes + num_bcs;

  for (size_t i = 0; i < equations.size(); ++i) {
    row_idx = global_add_idx + i;
    for (size_t j = 0; j < equations[i].terms.size(); ++j) {
      col_idx = dofs_per_elem * equations[i].terms[j].node_number +
                equations[i].terms[j].dof;
      Kg.insert(row_idx, col_idx) = equations[i].terms[j].coefficient;
      Kg.insert(col_idx, row_idx) = equations[i].terms[j].coefficient;
    }
  }
};

void loadTies(std::vector<Eigen::Triplet<double>> &triplets,
              const std::vector<Tie> &ties) {
  const unsigned int dofs_per_elem = DOF::NUM_DOFS;
  unsigned int nn1, nn2;
  double lmult, rmult, spring_constant;

  for (size_t i = 0; i < ties.size(); ++i) {
    nn1 = ties[i].node_number_1;
    nn2 = ties[i].node_number_2;
    lmult = ties[i].lmult;
    rmult = ties[i].rmult;

    for (unsigned int j = 0; j < dofs_per_elem; ++j) {
      // first 3 DOFs are linear DOFs, second 2 are rotational, last is
      // torsional
      spring_constant = j < 3 ? lmult : rmult;

      triplets.push_back(Eigen::Triplet<double>(
          dofs_per_elem * nn1 + j, dofs_per_elem * nn1 + j, spring_constant));

      triplets.push_back(Eigen::Triplet<double>(
          dofs_per_elem * nn2 + j, dofs_per_elem * nn2 + j, spring_constant));

      triplets.push_back(Eigen::Triplet<double>(
          dofs_per_elem * nn1 + j, dofs_per_elem * nn2 + j, -spring_constant));

      triplets.push_back(Eigen::Triplet<double>(
          dofs_per_elem * nn2 + j, dofs_per_elem * nn1 + j, -spring_constant));
    }
  }
};

std::vector<std::vector<double>>
computeTieForces(const std::vector<Tie> &ties,
                 const std::vector<std::vector<double>> &nodal_displacements) {
  const unsigned int dofs_per_elem = DOF::NUM_DOFS;
  unsigned int nn1, nn2;
  double lmult, rmult, spring_constant, delta1, delta2;

  std::vector<std::vector<double>> tie_forces(
      ties.size(), std::vector<double>(dofs_per_elem));

  for (size_t i = 0; i < ties.size(); ++i) {
    nn1 = ties[i].node_number_1;
    nn2 = ties[i].node_number_2;
    lmult = ties[i].lmult;
    rmult = ties[i].rmult;

    for (unsigned int j = 0; j < dofs_per_elem; ++j) {
      // first 3 DOFs are linear DOFs, second 2 are rotational, last is
      // torsional
      spring_constant = j < 3 ? lmult : rmult;
      delta1 = nodal_displacements[nn1][j];
      delta2 = nodal_displacements[nn2][j];
      tie_forces[i][j] = spring_constant * (delta2 - delta1);
    }
  }
  return tie_forces;
}

void loadForces(SparseMat &force_vec, const std::vector<Force> &forces) {
  const unsigned int dofs_per_elem = DOF::NUM_DOFS;
  unsigned int idx;

  for (size_t i = 0; i < forces.size(); ++i) {
    idx = dofs_per_elem * forces[i].node + forces[i].dof;
    force_vec.insert(idx, 0) = forces[i].value;
  }
};

Summary solve(const Job &job, const std::vector<BC> &BCs,
              const std::vector<Force> &forces, const std::vector<Tie> &ties,
              const std::vector<Equation> &equations, const Options &options) {
  auto initial_start_time = std::chrono::high_resolution_clock::now();

  Summary summary;
  summary.num_nodes = job.nodes.size();
  summary.num_elems = job.elems.size();
  summary.num_bcs = BCs.size();
  summary.num_ties = ties.size();

  const unsigned int dofs_per_elem = DOF::NUM_DOFS;

  // calculate size of global stiffness matrix and force vector
  const unsigned long size =
      dofs_per_elem * job.nodes.size() + BCs.size() + equations.size();

  // construct global stiffness matrix and force vector
  SparseMat Kg(size, size);
  SparseMat force_vec(size, 1);
  force_vec.reserve(forces.size() + BCs.size());

  // construct global assembler object and assemble global stiffness matrix
  auto start_time = std::chrono::high_resolution_clock::now();
  GlobalStiffAssembler assembleK3D = GlobalStiffAssembler();
  assembleK3D(Kg, job, ties);
  auto end_time = std::chrono::high_resolution_clock::now();
  auto delta_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time)
                        .count();
  summary.assembly_time_in_ms = delta_time;

  if (options.verbose)
    std::cout << "Global stiffness matrix assembled in " << delta_time
              << " ms.\nNow preprocessing factorization..." << std::endl;

  unsigned int numberOfDoF = DOF::NUM_DOFS * job.nodes.size();
#ifdef DEBUG_FILE
  Eigen::MatrixXd KgNoBCDense(Kg.block(0, 0, numberOfDoF, numberOfDoF));
  std::ofstream kgNoBCFile("KgNoBC.csv");
  if (kgNoBCFile.is_open()) {
    const static Eigen::IOFormat CSVFormat(Eigen::StreamPrecision,
                                           Eigen::DontAlignCols, ", ", "\n");
    kgNoBCFile << KgNoBCDense.format(CSVFormat) << '\n';
    kgNoBCFile.close();
  }
//    std::cout << KgNoBCDense << std::endl;
#endif
  // load prescribed boundary conditions into stiffness matrix and force vector
  loadBCs(Kg, force_vec, BCs, job.nodes.size());

  if (equations.size() > 0) {
    loadEquations(Kg, equations, job.nodes.size(), BCs.size());
  }

  // load prescribed forces into force vector
  if (forces.size() > 0) {
    loadForces(force_vec, forces);
  }
#ifdef DEBUG_FILE
  Eigen::MatrixXd KgDense(Kg);
  //    std::cout << KgDense << std::endl;
  Eigen::VectorXd forcesVectorDense(force_vec);
//    std::cout << forcesVectorDense << std::endl;
#endif
  // compress global stiffness matrix since all non-zero values have been added.
  Kg.prune(1.e-14);
  Kg.makeCompressed();
#ifdef DEBUG_FILE
  std::ofstream kgFile("Kg.csv");
  if (kgFile.is_open()) {
    const static Eigen::IOFormat CSVFormat(Eigen::StreamPrecision,
                                           Eigen::DontAlignCols, ", ", "\n");
    kgFile << KgDense.format(CSVFormat) << '\n';
    kgFile.close();
  }
  std::ofstream forcesFile("forces.csv");
  if (forcesFile.is_open()) {
    const static Eigen::IOFormat CSVFormat(Eigen::StreamPrecision,
                                           Eigen::DontAlignCols, ", ", "\n");
    forcesFile << forcesVectorDense.format(CSVFormat) << '\n';
    forcesFile.close();
  }
#endif
  // initialize solver based on whether MKL should be used
#ifdef EIGEN_USE_MKL_ALL
  Eigen::PardisoLU<SparseMat> solver;
#else
  Eigen::SparseLU<SparseMat> solver;

#endif

  // Compute the ordering permutation vector from the structural pattern of Kg
  start_time = std::chrono::high_resolution_clock::now();
  solver.analyzePattern(Kg);
  end_time = std::chrono::high_resolution_clock::now();
  delta_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                                     start_time)
                   .count();

  summary.preprocessing_time_in_ms = delta_time;

  if (options.verbose)
    std::cout << "Preprocessing step of factorization completed in "
              << delta_time
              << " ms.\nNow factorizing global stiffness matrix..."
              << std::endl;

  // Compute the numerical factorization
  start_time = std::chrono::high_resolution_clock::now();
  solver.factorize(Kg);
  std::cout << solver.lastErrorMessage() << std::endl;
  assert(solver.info() == Eigen::Success);
  end_time = std::chrono::high_resolution_clock::now();

  delta_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                                     start_time)
                   .count();
  summary.factorization_time_in_ms = delta_time;

  if (options.verbose)
    std::cout << "Factorization completed in " << delta_time
              << " ms. Now solving system..." << std::endl;

  // Use the factors to solve the linear system
  start_time = std::chrono::high_resolution_clock::now();
  SparseMat dispSparse = solver.solve(force_vec);
  end_time = std::chrono::high_resolution_clock::now();
  delta_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                                     start_time)
                   .count();

  summary.solve_time_in_ms = delta_time;

  if (options.verbose)
    std::cout << "System was solved in " << delta_time << " ms.\n" << std::endl;

  // convert to dense matrix
  Eigen::VectorXd disp(dispSparse);

  // convert from Eigen vector to std vector
  std::vector<std::vector<double>> disp_vec(job.nodes.size(),
                                            std::vector<double>(dofs_per_elem));
  for (size_t i = 0; i < disp_vec.size(); ++i) {
    for (unsigned int j = 0; j < dofs_per_elem; ++j)
      // round all values close to 0.0
      disp_vec[i][j] = std::abs(disp(dofs_per_elem * i + j)) < options.epsilon
                           ? 0.0
                           : disp(dofs_per_elem * i + j);
  }
  summary.nodal_displacements = disp_vec;

  // [calculate nodal forces
  start_time = std::chrono::high_resolution_clock::now();

  Kg = Kg.topLeftCorner(dofs_per_elem * job.nodes.size(),
                        dofs_per_elem * job.nodes.size());
  dispSparse = dispSparse.topRows(dofs_per_elem * job.nodes.size());

  SparseMat nodal_forces_sparse = Kg * dispSparse;

  Eigen::VectorXd nodal_forces_dense(nodal_forces_sparse);

  std::vector<std::vector<double>> nodal_forces_vec(
      job.nodes.size(), std::vector<double>(dofs_per_elem));
  for (size_t i = 0; i < nodal_forces_vec.size(); ++i) {
    for (unsigned int j = 0; j < dofs_per_elem; ++j)
      // round all values close to 0.0
      nodal_forces_vec[i][j] =
          std::abs(nodal_forces_dense(dofs_per_elem * i + j)) < options.epsilon
              ? 0.0
              : nodal_forces_dense(dofs_per_elem * i + j);
  }
  summary.nodal_forces = nodal_forces_vec;

  end_time = std::chrono::high_resolution_clock::now();

  summary.nodal_forces_solve_time_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                            start_time)
          .count();
  //]

  // [ calculate forces associated with ties
  if (ties.size() > 0) {
    start_time = std::chrono::high_resolution_clock::now();
    summary.tie_forces = computeTieForces(ties, disp_vec);
    end_time = std::chrono::high_resolution_clock::now();
    summary.tie_forces_solve_time_in_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                              start_time)
            .count();
  }
  // ]

  // [save files specified in options
  CSVParser csv;
  start_time = std::chrono::high_resolution_clock::now();
  if (options.save_nodal_displacements) {
    std::cout << "Writing to:" + options.nodal_displacements_filename
              << std::endl;
    csv.write(options.nodal_displacements_filename, disp_vec,
              options.csv_precision, options.csv_delimiter);
  }

  if (options.save_nodal_forces) {
    csv.write(options.nodal_forces_filename, nodal_forces_vec,
              options.csv_precision, options.csv_delimiter);
  }

  if (options.save_tie_forces) {
    csv.write(options.tie_forces_filename, summary.tie_forces,
              options.csv_precision, options.csv_delimiter);
  }

  end_time = std::chrono::high_resolution_clock::now();
  summary.file_save_time_in_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                            start_time)
          .count();
  // ]

  auto final_end_time = std::chrono::high_resolution_clock::now();

  delta_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                   final_end_time - initial_start_time)
                   .count();
  summary.total_time_in_ms = delta_time;

  if (options.save_report) {
    writeStringToTxt(options.report_filename, summary.FullReport());
  }

  if (options.verbose)
    std::cout << summary.FullReport();

  // Compute per element forces
  summary.element_forces.resize(job.elems.size(),
                                std::vector<double>(2 * DOF::NUM_DOFS));
  std::vector<LocalMatrix> perElemKlocalAelem =
      assembleK3D.getPerElemKlocalAelem();

  for (size_t elemIndex = 0; elemIndex < job.elems.size(); elemIndex++) {
    // Assemble the displacements of the nodes of the beam
    Eigen::Matrix<double, 12, 1> elemDisps;
    // Displacements of the first node of the beam
    const size_t nodeIndex0 = job.elems[elemIndex](0);
    elemDisps(0) = summary.nodal_displacements[nodeIndex0][0];
    elemDisps(1) = summary.nodal_displacements[nodeIndex0][1];
    elemDisps(2) = summary.nodal_displacements[nodeIndex0][2];
    elemDisps(3) = summary.nodal_displacements[nodeIndex0][3];
    elemDisps(4) = summary.nodal_displacements[nodeIndex0][4];
    elemDisps(5) = summary.nodal_displacements[nodeIndex0][5];
    // Displacemen ts of the second node of the beam
    const size_t nodeIndex1 = job.elems[elemIndex](1);
    elemDisps(6) = summary.nodal_displacements[nodeIndex1][0];
    elemDisps(7) = summary.nodal_displacements[nodeIndex1][1];
    elemDisps(8) = summary.nodal_displacements[nodeIndex1][2];
    elemDisps(9) = summary.nodal_displacements[nodeIndex1][3];
    elemDisps(10) = summary.nodal_displacements[nodeIndex1][4];
    elemDisps(11) = summary.nodal_displacements[nodeIndex1][5];

    Eigen::Matrix<double, 12, 1> elemForces =
        perElemKlocalAelem[elemIndex] * elemDisps;
    for (int dofIndex = 0; dofIndex < DOF::NUM_DOFS; dofIndex++) {
      summary.element_forces[elemIndex][static_cast<size_t>(dofIndex)] =
          -elemForces(dofIndex);
    }
    // meaning of sign = reference to first node:
    // + = compression for axial, - traction
    for (int dofIndex = DOF::NUM_DOFS; dofIndex < 2 * DOF::NUM_DOFS;
         dofIndex++) {
      summary.element_forces[elemIndex][static_cast<size_t>(dofIndex)] =
          +elemForces(dofIndex);
    }
  }

  if (options.save_elemental_forces) {
    std::cout << "Writing to:" + options.elemental_forces_filename << std::endl;
    csv.write(options.elemental_forces_filename, summary.element_forces,
              options.csv_precision, options.csv_delimiter);
  }

  return summary;
};

} // namespace fea
