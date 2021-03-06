#include <string>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <vector>
#include <iterator>

#include "genomics/sequences.hpp"
#include "genomics/seq_io.hpp"

namespace genomics {
    namespace seq_io {
        namespace {
            inline void ltrim(std::string &s) {
                s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
                                                                        return !std::isspace(ch);
                                                                    }));
            }

            inline void rtrim(std::string &s) {
                s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
                                                               return !std::isspace(ch);
                                                           }).base(), s.end());
            }

            inline char my_toupper(char ch)
            {
                return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }

	    template <typename Out>
	    void split(const std::string &s, char delim, Out result) {
		std::istringstream iss(s);
		std::string item;
		while (std::getline(iss, item, delim)) {
		    *result++ = item;
		}
	    }

	    std::vector<std::string> split(const std::string &s, char delim) {
		std::vector<std::string> elems;
		split(s, delim, std::back_inserter(elems));
		return elems;
	    }

            void convert_raw_sequence(std::string& seq) {
                ltrim(seq);
                rtrim(seq);

                for (size_t i = 0; i < seq.length(); i++) {
                    seq[i] = toupper(seq[i]);
                }
            }
        };

        void parse_sequence(std::istream& fasta_is, std::ostream& sequence_os) {
            for(std::string line; std::getline(fasta_is, line);) {
                if (line.length() > 0 && line[0] == '>') continue;
                convert_raw_sequence(line);
                sequence_os << line;
            }
        }

        void reverse_complement_stream(std::istream& sequence_is, std::ostream& sequence_os) {
            std::string sequence;
            while (sequence_is) {
                sequence_is >> sequence;
            }

            sequence_os << genomics::reverse_complement(sequence);
        }

        genome_structure parse_genome_structure(std::istream& fasta_is) {
            genome_structure gs;

            std::string chromosome_name, line;
            std::getline(fasta_is, line);
            if (line.size() < 1 || line[0] != '>') {
                return gs;
            }

            chromosome_name = line.substr(1);
            ltrim(chromosome_name);
            rtrim(chromosome_name);
	    auto words = split(chromosome_name, ' ');
	    chromosome_name = words[0];
            size_t length = 0;
        
            while(std::getline(fasta_is, line)) {
                if (line[0] == '>') {
                    chromosome c = {chromosome_name, length};
                    gs.push_back(c);
                    chromosome_name = line.substr(1);
                    ltrim(chromosome_name);
                    rtrim(chromosome_name);
		    auto words = split(chromosome_name, ' ');
		    chromosome_name = words[0];
                    length = 0;
                    continue;
                }

                length += line.length();
            }

            chromosome c = {chromosome_name, length};
            gs.push_back(c);

            return gs;
        }

        void write_to_file(const genome_structure& gs, const std::string& filename){
            std::ofstream fs;
            fs.open(filename);
        
            for (auto p : gs) {
                fs << p.name;
                fs << "\n";
                fs << p.length;
                fs << "\n";
            }
        }

        bool load_from_file(genome_structure& gs, const std::string& filename){
            std::ifstream fs(filename);

            if (!fs) return false;
        
            while (fs) {
                std::string name, length_str;
                std::getline(fs, name);
                std::getline(fs, length_str);

                if (name.length() == 0 || length_str.length() == 0) {
                    break;
                }

                size_t length = std::stoll(length_str);
                chromosome c = {name, length};
                gs.push_back(c);
            }

            return true;
        }

	void write_to_file(const std::vector<kmer>& kmers, const std::string& filename) {
            std::ofstream fs;
            fs.open(filename);
        
            for (auto kmer : kmers) {
                fs << kmer.sequence << " ";
                fs << kmer.pam << " ";
                fs << kmer.absolute_coords << " ";
                fs << (kmer.dir == direction::positive ? "+" : "-") << std::endl;
            }
	}

        void write_to_file(kmer_producer& kmer_p, const std::string& filename) {
            std::ofstream fs;
            fs.open(filename);
        
            kmer kmer;
            while (kmer_p.get_next_kmer(kmer)) {
                fs << kmer.sequence << " ";
                fs << kmer.pam << " ";
                fs << kmer.absolute_coords << " ";
                fs << (kmer.dir == direction::positive ? "+" : "-") << std::endl;
            }
	}

	size_t parse_kmer(std::istream& kmers_stream, kmer& out_kmer) {
	    if (!kmers_stream) return 0;

	    std::string line;
	    std::getline(kmers_stream, line);
	    auto words = split(line, ' ');

	    if (words.size() < 4) return 0;

	    std::istringstream iss(words[2]);
	    size_t coords;
	    iss >> coords;

	    direction dir = (words[3] == "+") ? direction::positive : direction::negative;
	    out_kmer = {words[0], words[1], coords, dir};
	    return 1;
	}

	bool load_from_file(std::vector<kmer>& kmers, const std::string& filename) {
	    std::ifstream fs(filename);

            if (!fs) return false;
        
	    kmer k;
            while (parse_kmer(fs, k)) {
		kmers.push_back(k);
	    }

	    return true;
	}
    }; 
};
