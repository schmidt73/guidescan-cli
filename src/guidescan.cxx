#include <thread>
#include <istream>

#include <sdsl/suffix_arrays.hpp>

#include "cxxopts.hpp"
#include "genomics/index.hpp"
#include "genomics/seq_io.hpp"
#include "genomics/process.hpp"

cxxopts::Options create_options() {
    cxxopts::Options options("guidescan", "Guidescan interface for gRNA database generation.");

    options.add_options()
        ("k,kmerlength", "Length of kmers",
         cxxopts::value<size_t>() -> default_value("20"), "LENGTH")
        ("p,pam", "PAM string",
         cxxopts::value<std::string>() -> default_value("NGG"), "PAM")
        ("n,threads", "Number of threads to run across",
         cxxopts::value<size_t>(), "NTHREADS")
        ("g,genome", "Genome in FASTA .fna format",
         cxxopts::value<std::string>(), "GENOME")
        ("v,verbose", "Enable verbosity")
        ("h,help", "Print help");

    options.parse_positional({"genome"});
    options.positional_help("[genome]");

    return options;
}

bool file_exists(const std::string& fileName)
{
    std::ifstream infile(fileName);
    return infile.good();
}

int main(int argc, char *argv[])
{
    using namespace std;
    
    auto options = create_options();

    string fasta_file, pam;
    size_t nthreads, kmer_length;
    
    try {
        auto result = options.parse(argc, argv);

        if (result["genome"].count() < 1 || result["help"].count() > 0) {
            cout << options.help() << endl;
            return 1;
        }

        fasta_file  = result["genome"].as<string>();
        pam         = result["pam"].as<string>();
        kmer_length = result["kmerlength"].as<size_t>();
        nthreads    = result["threads"].count() ?
            result["threads"].as<size_t>() : thread::hardware_concurrency();
    } catch (const cxxopts::OptionParseException& e) {
        std::cout << e.what() << std::endl;
        std::exit(1);
    }

    string raw_sequence_file = fasta_file + ".dna";
    string genome_structure_file = fasta_file + ".gs";
    string fm_index_file = fasta_file + ".csa";

    ifstream fasta_is(fasta_file);
    if (!fasta_is) {
        cerr << "ERROR: FASTA file \"" << fasta_file
             << "\" does not exist." << endl;
        return 1;
    }

    if (!file_exists(raw_sequence_file)) {
        ofstream os(raw_sequence_file);
        if (!os) {
            cerr << "ERROR: Could not create raw sequence file." << endl;
            return 1;
        }

        cout << "No raw sequence file \"" << raw_sequence_file
             << "\". Building now..." << endl;
        genomics::seq_io::parse_sequence(fasta_is, os);
    }

    genomics::genome_structure gs;
    if (!genomics::seq_io::load_from_file(gs, genome_structure_file)) {
        cout << "No genome structure file \"" << genome_structure_file
             << "\" located. Building now..." << endl;

        fasta_is.clear();
        fasta_is.seekg(0);
        gs = genomics::seq_io::parse_genome_structure(fasta_is);
        genomics::seq_io::write_to_file(gs, genome_structure_file);
    }

    sdsl::csa_wt<sdsl::wt_huff<>, 32, 8192> fm_index;
    if (!load_from_file(fm_index, fm_index_file)) {
        cout << "No index file \"" << fm_index_file
             << "\" located. Building now..." << endl;

        construct(fm_index, raw_sequence_file, 1);
        store_to_file(fm_index, fm_index_file);
    }   


    genomics::genome_index<sdsl::wt_huff<>, 32, 8192> gi(fm_index, gs);
    cout << "Successfully loaded index." << endl;

    vector<thread> threads;
    for (int i = 0; i < nthreads; i++) {
        thread t(genomics::process_kmers_to_stream<sdsl::wt_huff<>, 32, 8192>,
                 cref(gi), cref(raw_sequence_file), kmer_length, pam, ref(cout),
                 i, nthreads);
        threads.push_back(move(t));
    }

    for (auto &thread : threads) {
        thread.join();
    }
    
    return 0;
}
