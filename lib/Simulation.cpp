#include "Simulation.hpp"
#include "Utils.hpp"
#include "particle.hpp"
#include <cstring>
#include <cmath>
#include <iostream>
#include <omp.h>
#include <filesystem>

Simulation::Simulation(double t_max, double t_step, particle_group collection, double W, uint num_cells, double e_factor) : 
                        time_max(t_max), time_step(t_step), particle_collection(collection), box_width(W), number_of_cells(num_cells),
                         expansion_factor(e_factor)
{
    if (t_max <= 0){
        throw std::invalid_argument("Error - t_max (maximum time reached) must not be less than or equal to 0!");
    }
    if (t_step <= 0){
        throw std::invalid_argument("Error - t_step (time step) must not be less than or equal to 0!");
    }
    if (W <= 0){
        throw std::invalid_argument("Error - W (Box Width) must not be less than or equal to 0!");
    }
    if (e_factor <= 0){
        throw std::invalid_argument("Error - e_factor (expansion factor) must be larger than 0!");
    }
    else if (e_factor < 1){
        std::cerr << "Warning - e_factor is less than 1 so simulatation will represent contracting universe. This is functional however is unphysical." << std::endl;
    }
    if (num_cells > std::numeric_limits<int>::max()){
        throw std::overflow_error("Error - The number of cells stated is invalid.");
    }
    if (num_cells > 400){
        std::cerr << "Warning - num_cells (Grid Length) has been set to more than 400 units! This may have adverse effects on performance." << std::endl;
    }
    // allocate and instantiate density buffer
    uint buffer_length = number_of_cells * number_of_cells * number_of_cells;
    density_buffer = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * buffer_length);
    potential_buffer = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * buffer_length);
    k_space_buffer = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * buffer_length);

    // Efficiently zero-initialize the buffers
    std::memset(density_buffer, 0, sizeof(fftw_complex) * buffer_length);
    std::memset(potential_buffer, 0, sizeof(fftw_complex) * buffer_length);
    std::memset(k_space_buffer, 0, sizeof(fftw_complex) * buffer_length);

    // assign plans
    forward_plan = fftw_plan_dft_3d(number_of_cells, number_of_cells, number_of_cells, density_buffer, k_space_buffer, FFTW_FORWARD, FFTW_MEASURE);
    backward_plan = fftw_plan_dft_3d(number_of_cells, number_of_cells, number_of_cells, k_space_buffer, potential_buffer, FFTW_BACKWARD, FFTW_MEASURE);
}


Simulation::~Simulation(){
    fftw_free(density_buffer); // deallocate manually allocated memory in heap to prevent memory leak
    fftw_free(potential_buffer);
    fftw_free(k_space_buffer);

    fftw_destroy_plan(forward_plan);
    fftw_destroy_plan(backward_plan);
}

void Simulation::run(std::optional<std::string> output_folder)
{
    std::string ppc = findsigfig(static_cast<double>(particle_collection.get_num_particles())/static_cast<double>(number_of_cells * number_of_cells * number_of_cells));
    
    double t = 0.0;
    uint counter = 0;
    while (t < time_max){
        fill_density_buffer();
        fill_potential_buffer();
        update_particles();
        box_expansion();
        t += time_step;
        
        if (output_folder){
            counter++;
            if (counter >= 10){
                counter = 0;
                std::string partial_path = *output_folder + "/" + findsigfig(expansion_factor) + "/"; // directories to be stored
                std::filesystem::create_directories(partial_path);
                std::string full_path = partial_path + "UniverseSim_dt_" + findsigfig(time_step) + "_time_" + 
                findsigfig(t) + "_num_cells_" + std::to_string(number_of_cells) + "_ppc_" + ppc + ".pbm";
                SaveToFile(density_buffer, number_of_cells, full_path);
            }
        }
    }
}

void Simulation::fill_density_buffer(){
    std::memset(density_buffer, 0, sizeof(fftw_complex) * number_of_cells * number_of_cells * number_of_cells); // initialise density buffer to 0
    
    #pragma omp parallel for
    for (size_t particle_index = 0; particle_index < particle_collection.get_num_particles(); particle_index++){ // iterate through every particle and evaluate position
        particle& current_particle = particle_collection.particles[particle_index];
        uint i = std::floor(current_particle.position[0] * number_of_cells);
        uint j = std::floor(current_particle.position[1] * number_of_cells);
        uint k = std::floor(current_particle.position[2] * number_of_cells);
        
        uint index = k + number_of_cells * (j + number_of_cells * i);
        double cell_width = (box_width/number_of_cells);
        double single_density = particle_collection.mass / (cell_width * cell_width * cell_width);
        // use of atomic to prevent race condition when updating density buffer
        //#pragma omp critical
        #pragma omp atomic
        density_buffer[index][0] += single_density;
    }
}

void Simulation::fill_potential_buffer(){
    uint total_size = number_of_cells * number_of_cells * number_of_cells;
    fftw_execute(forward_plan);
    k_space_buffer[0][0] = 0; //set first element of the buffer to 0.
    k_space_buffer[0][1] = 0;
    
    #pragma omp parallel for //parallelise
    for (uint index = 1; index < total_size; index++){
        uint i = index / (number_of_cells * number_of_cells);
        uint j = (index / number_of_cells) % number_of_cells;
        uint k = index % number_of_cells;
        
        double cell_num = number_of_cells; //cast to double
        double norm_factor = -4 * M_PI * box_width * box_width/(i * i + j * j + k * k) * 
            (1/(8 * cell_num * cell_num * cell_num)); //scale by -4*pi/k^2 and normalisation factor
    
        k_space_buffer[index][0] *= norm_factor;
        k_space_buffer[index][1] *= norm_factor;
    }
    fftw_execute(backward_plan);
}

std::vector<std::vector<std::vector<std::array<double, 3>>>> Simulation::calculate_gradient(const fftw_complex * potential){
    double cell_width = box_width/number_of_cells;

    std::vector<std::vector<std::vector<std::array<double, 3>>>> gradient(number_of_cells, std::vector<std::vector<std::array<double, 3>>>(
        number_of_cells, std::vector<std::array<double, 3>>(number_of_cells, std::array<double, 3>{0, 0, 0}))); // Initialize each std::array<double, 3> with zeros
    
    //#pragma omp parallel for
    #pragma omp parallel for collapse(3) // Parallelize all nested loops
    for (int i = 0; i < number_of_cells; i++){
        for (int j = 0; j < number_of_cells; j++){
            for (int k = 0; k < number_of_cells; k++){
                int i_high = i + 1;
                int i_low = i - 1;
                int j_high = j + 1;
                int j_low = j - 1;
                int k_high = k + 1;
                int k_low = k - 1;

                while (i_high >= number_of_cells){i_high -= number_of_cells;}
                while (i_low < 0){i_low += number_of_cells;}
                while (j_high >= number_of_cells){j_high -= number_of_cells;}
                while (j_low < 0){j_low += number_of_cells;}
                while (k_high >= number_of_cells){k_high -= number_of_cells;}
                while (k_low < 0){k_low += number_of_cells;}

                gradient[i][j][k][0] = (potential[k + number_of_cells * (j + number_of_cells * i_high)][0] 
                - potential[k + number_of_cells * (j + number_of_cells * i_low)][0])/(2 * cell_width);

                gradient[i][j][k][1] = (potential[k + number_of_cells * (j_high + number_of_cells * (i))][0] 
                - potential[k + number_of_cells * (j_low + number_of_cells * i)][0])/(2 * cell_width);
                
                gradient[i][j][k][2] = (potential[k_high + number_of_cells * (j + number_of_cells * (i))][0] 
                - potential[k_low + number_of_cells * (j + number_of_cells * i)][0])/(2 * cell_width);
            }
        }
    }
    return gradient;
}

void Simulation::update_particles(){
    std::vector<std::vector<std::vector<std::array<double, 3>>>> gradient = calculate_gradient(potential_buffer);
    
    #pragma omp parallel for
    for (size_t index = 0; index < particle_collection.get_num_particles(); index++){
        particle& current_particle = particle_collection.particles[index];
        uint i = std::floor(current_particle.position[0] * number_of_cells);
        uint j = std::floor(current_particle.position[1] * number_of_cells);
        uint k = std::floor(current_particle.position[2] * number_of_cells);

        current_particle.velocity[0] += -1 * gradient[i][j][k][0] * time_step;
        current_particle.velocity[1] += -1 * gradient[i][j][k][1] * time_step;
        current_particle.velocity[2] += -1 * gradient[i][j][k][2] * time_step;

        current_particle.position[0] += current_particle.velocity[0] * time_step;
        current_particle.position[1] += current_particle.velocity[1] * time_step;
        current_particle.position[2] += current_particle.velocity[2] * time_step;

        // apply boundary conditions
        while (current_particle.position[0] < 0){current_particle.position[0] +=1;}
        while (current_particle.position[0] >= 1){current_particle.position[0] -= 1;}
        while (current_particle.position[1] < 0){current_particle.position[1] += 1;}
        while (current_particle.position[1] >= 1){current_particle.position[1] -= 1;}
        while (current_particle.position[2] < 0){current_particle.position[2] += 1;}
        while (current_particle.position[2] >= 1){current_particle.position[2] -= 1;}
    }
}

void Simulation::box_expansion(){
    box_width *= expansion_factor;

    #pragma omp parallel for
    for (size_t i = 0; i < particle_collection.get_num_particles(); i++){
        particle_collection.particles[i].velocity[0] /= expansion_factor;
        particle_collection.particles[i].velocity[1] /= expansion_factor;
        particle_collection.particles[i].velocity[2] /= expansion_factor;
    }
}


const fftw_complex* Simulation::get_density_buffer() const {
    return density_buffer;
}

const fftw_complex* Simulation::get_potential_buffer() const{
    return potential_buffer;
}

const particle_group & Simulation::get_particle_collection() const {
    return particle_collection;
}