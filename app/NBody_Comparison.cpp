#include <mpi.h>
#include "Utils.hpp"
#include <filesystem>
#include "Simulation.hpp"

int main(int argc, char** argv) 
{
    MPI_Init(&argc, &argv);
    int process_id;
    int num_proc;
    MPI_Comm_rank(MPI_COMM_WORLD, &process_id);
    MPI_Comm_size(MPI_COMM_WORLD, &num_proc);
    uint num_bins = 101;
    if (process_id == 0){
        std::string output_folder;
        double minimum_expansion_factor;
        double maximum_expansion_factor;
        
        for (uint i = 1; i < argc; i+=2){
            std::string arg(argv[i]);
            if (arg == "-o"){
                output_folder = argv[i+1];
            }
            else if (arg == "-emin"){
                std::string arg1(argv[i+1]);
                minimum_expansion_factor = std::stod(arg1.c_str());
            }
            else if (arg == "-emax"){
                std::string arg1(argv[i+1]);
                maximum_expansion_factor = std::stod(arg1.c_str());
            }
            else{ // extra error handling
                std::string arg1(arg);
                //fprintf(stderr,"%s\n","Invalid Flag Detected: " + arg1);
                MPI_Abort(MPI_COMM_WORLD, 0);
            }
        }
        uint num_cells = 101;
        uint average_particles_per_cell = 13;
        double width = 100.0;
        uint num_particles = num_cells * num_cells * num_cells * average_particles_per_cell;
        double mass = 10.0 * 10.0 * 10.0 * 10.0 * 10.0/num_particles;
        double expansion_factor_step = (maximum_expansion_factor - minimum_expansion_factor)/(num_proc - 1);
        double expansion_factor = minimum_expansion_factor + process_id * expansion_factor_step;
        std::vector<std::string> expansion_fac_vec = {findsigfig(expansion_factor)};

        for (int i = 1; i < num_proc; i++){
            MPI_Send(&minimum_expansion_factor, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD);
            MPI_Send(&expansion_factor_step, 1, MPI_DOUBLE, i, 1, MPI_COMM_WORLD);
        }
        uint random_seed = 42;
        double t_max = 1.5;
        double time_step = 0.01;
        Simulation sim(t_max, time_step, particle_group(mass, num_particles, random_seed), width, num_cells, expansion_factor);
        sim.run();
        const particle_group particle_collection = sim.get_particle_collection();
        std::vector<double> corr_func = correlationFunction(particle_collection, num_bins);
        std::vector<std::vector<double>> corr_funcs = {corr_func,};
        for (int i = 1; i < num_proc; i++){
            // collect expansion_factors into vector
            expansion_fac_vec.push_back(findsigfig(minimum_expansion_factor + i * expansion_factor_step));
            
            // receive data from non-master processes
            int receive_size;
            MPI_Recv(&receive_size, 1, MPI_INT, i, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            std::vector<double> receive_vec(receive_size);
            MPI_Recv(receive_vec.data(), receive_size, MPI_DOUBLE, i, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            corr_funcs.push_back(receive_vec);
        }
        std::string filepath = output_folder + "/Comparison_" + std::to_string(num_proc) + "_" + findsigfig(minimum_expansion_factor) + "_" 
        + findsigfig(maximum_expansion_factor) + ".csv";

        std::filesystem::create_directories(output_folder);
        Save_Correlations_csv(corr_funcs, expansion_fac_vec, filepath);
    }
    else{
        double expansion_factor_step;
        double minimum_expansion_factor;

        uint num_cells = 101;
        uint average_particles_per_cell = 13;
        double width = 100.0;
        uint num_particles = num_cells * num_cells * num_cells * average_particles_per_cell;
        double mass = 10.0 * 10.0 * 10.0 * 10.0 * 10.0/num_particles;

        MPI_Recv(&expansion_factor_step, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&minimum_expansion_factor, 1, MPI_DOUBLE, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        double expansion_factor = minimum_expansion_factor + process_id * expansion_factor_step;
        uint random_seed = 42;
        double t_max = 1.5;
        double time_step = 0.01;

        Simulation sim(t_max, time_step, particle_group(mass, num_particles, random_seed), width, num_cells, expansion_factor);
        sim.run();
        const particle_group particle_collection = sim.get_particle_collection();
        std::vector<double> corr_func = correlationFunction(particle_collection, num_bins);
        MPI_Send(&num_bins, 1, MPI_UNSIGNED, 0, 2, MPI_COMM_WORLD);
        MPI_Send(corr_func.data(), num_bins, MPI_DOUBLE, 0, 3, MPI_COMM_WORLD);

    }
    MPI_Finalize();
}