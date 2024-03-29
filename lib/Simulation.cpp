#include "Simulation.hpp"
#include "Utils.hpp"
#include "particle.hpp"
#include <cstring>

Simulation::Simulation(double t_max, double t_step, particle_group collection, double W, uint num_cells, double e_factor) : 
                        time_max(t_max), time_step(t_step), particle_collection(collection), box_width(W), number_of_cells(num_cells),
                         expansion_factor(e_factor)
{
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
    forward_plan = fftw_plan_dft_3d(number_of_cells, number_of_cells, number_of_cells, density_buffer, potential_buffer, FFTW_FORWARD, FFTW_MEASURE);
    backward_plan = fftw_plan_dft_3d(number_of_cells, number_of_cells, number_of_cells, k_space_buffer, potential_buffer, FFTW_BACKWARD, FFTW_MEASURE);
}


Simulation::~Simulation(){
    fftw_free(density_buffer);
    fftw_free(potential_buffer);
    fftw_free(k_space_buffer);

    fftw_destroy_plan(forward_plan);
    fftw_destroy_plan(backward_plan);
}

void Simulation::run()
{

}

std::vector<std::vector<std::vector<uint>>> Simulation::bin_particles(){
    std::vector<std::vector<std::vector<uint>>> counts(number_of_cells, std::vector<std::vector<uint>>(number_of_cells, std::vector<uint>(number_of_cells, 0)));
    //count how many particles in each bin
    for (uint i = 0; i < particle_collection.num_particles; i++){
        particle& current_particle = particle_collection.particles[i];
        counts[std::floor(current_particle.position[0] * number_of_cells)][std::floor(current_particle.position[1] * number_of_cells)]
        [std::floor(current_particle.position[2] * number_of_cells)]++;
    }
    return counts;
}

void Simulation::fill_density_buffer(){
    std::vector<std::vector<std::vector<uint>>> counts = bin_particles();
    for (uint i = 0; i < number_of_cells; i++){
        for (uint j = 0; j < number_of_cells; j++){
            for (uint k = 0; k < number_of_cells; k++){
                uint index = k + number_of_cells * (j + number_of_cells * i);
                double cell_width = (box_width/number_of_cells);
                density_buffer[index][0] = counts[i][j][k] * particle_collection.mass / (cell_width * cell_width * cell_width);
            }
        }
    }
}

void Simulation::fill_potential_buffer(){
    fftw_execute(forward_plan);
    // TODO: finish


    fftw_execute(backward_plan);
}

const fftw_complex* Simulation::get_density_buffer() const {
    return density_buffer;
}