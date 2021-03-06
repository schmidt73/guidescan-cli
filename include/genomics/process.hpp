#ifndef PROCESS_H
#define PROCESS_H

#include <set>
#include <tuple>

#include "json.hpp"
#include "genomics/kmer.hpp"
#include "genomics/sequences.hpp"
#include "genomics/sam.hpp"

namespace genomics {
    namespace {
        void off_target_enumerator(size_t sp, size_t ep, size_t k,
                                   std::vector<std::set<std::tuple<size_t, size_t>>> &off_targets_bwt) {
            off_targets_bwt[k].insert(std::make_tuple(sp, ep));
        }

        std::function<void(size_t, size_t, size_t, std::vector<std::set<std::tuple<size_t, size_t>>>&)> callback = off_target_enumerator;

        size_t count_off_targets(size_t k, const std::vector<std::set<std::tuple<size_t, size_t>>> &off_targets_bwt) {
            size_t count = 0;
            for (const auto& sp_ep : off_targets_bwt[k]) {
                size_t sp = std::get<0>(sp_ep);
                size_t ep = std::get<1>(sp_ep);
                count += ep - sp + 1;
            }
            return count;
        }

        void off_target_counter(size_t sp, size_t ep, size_t k, size_t &count) {
            count += ep - sp + 1;
        }

        std::function<void(size_t, size_t, size_t, size_t&)> counting_callback = off_target_counter;
    };

    template <class t_wt, uint32_t t_dens, uint32_t t_inv_dens>
    void process_kmer_to_stream(const genome_index<t_wt, t_dens, t_inv_dens>& gi_forward,
                                const genome_index<t_wt, t_dens, t_inv_dens>& gi_reverse,
                                const std::vector<std::string> &pams, size_t mismatches,
                                int threshold,
                                const kmer& k,
                                std::ostream& output,
                                std::mutex& output_mtx) {
        coordinates coords = resolve_absolute(gi_forward.gs, k.absolute_coords);
        size_t count = 0;

        /* Because of the way inexact searching is implemented (from
         * back-to-front) I search for the reverse complement of the
         * kmer on the reverse complement strand (which is essentially
         * searching forward) and I search for the reverse complement
         * of the kmer on the forward strand.
         */

        std::vector<std::string> pams_c;
        for (const auto& pam : pams) {
            pams_c.push_back(genomics::reverse_complement(pam));
        }
        
        std::string kmer = genomics::reverse_complement(k.sequence);
        if (threshold > 0) {
            gi_forward.inexact_search(kmer, pams_c, threshold, counting_callback, count);
            if (count > 1) return;
            gi_reverse.inexact_search(kmer, pams_c, threshold, counting_callback, count);
            if (count > 1) return;
        }

        std::vector<std::set<std::tuple<size_t, size_t>>> forward_off_targets_bwt(mismatches + 1);
        std::vector<std::set<std::tuple<size_t, size_t>>> reverse_off_targets_bwt(mismatches + 1);
        gi_forward.inexact_search(kmer, pams_c, mismatches, callback, forward_off_targets_bwt);
        gi_reverse.inexact_search(kmer, pams_c, mismatches, callback, reverse_off_targets_bwt);

        size_t genome_length = 0;
        for (int i = 0; i < gi_forward.gs.size(); i++) {
            genome_length += gi_forward.gs[i].length;
        }
        
        /*
         * This code resolves the position of the guide on the FORWARD
         * strand, making guides on the antisense strand negative so
         * that they can be distinguished.
         */

        std::vector<std::vector<int64_t>> off_targets(mismatches + 1);
        for (int i = 0; i < mismatches + 1; i++) {
            for (const auto& sp_ep : forward_off_targets_bwt[i]) {
                size_t sp = std::get<0>(sp_ep);
                size_t ep = std::get<1>(sp_ep);
                for (size_t j = sp; j <= ep; j++) {
                    int64_t absolute_pos = -gi_forward.resolve(j);
                    off_targets[i].push_back(absolute_pos);
                }
            }

            for (const auto& sp_ep : reverse_off_targets_bwt[i]) {
                size_t sp = std::get<0>(sp_ep);
                size_t ep = std::get<1>(sp_ep);
                for (size_t j = sp; j <= ep; j++) {
                    int64_t absolute_pos = genome_length - (gi_reverse.resolve(j) + 1);
                    off_targets[i].push_back(absolute_pos);
                }
            }
        }

        std::string sam_line = genomics::get_sam_line(output, gi_forward, k, coords, off_targets);

        output_mtx.lock();
        output << sam_line << std::endl;
        output_mtx.unlock();
    }


    template <class t_wt, uint32_t t_dens, uint32_t t_inv_dens>
    nlohmann::json search_kmer(const genome_index<t_wt, t_dens, t_inv_dens>& gi_forward,
                               const genome_index<t_wt, t_dens, t_inv_dens>& gi_reverse,
                               std::string kmer, size_t mismatches) {
        using json = nlohmann::json;
        std::vector<std::set<std::tuple<size_t, size_t>>> forward_matches(mismatches + 1);
        std::vector<std::set<std::tuple<size_t, size_t>>> reverse_matches(mismatches + 1);

        gi_forward.inexact_search(kmer.begin(), kmer.end(), mismatches, callback, forward_matches);
        gi_reverse.inexact_search(kmer.begin(), kmer.end(), mismatches, callback, reverse_matches);

        size_t genome_length = 0;
        for (int i = 0; i < gi_forward.gs.size(); i++) {
            genome_length += gi_forward.gs[i].length;
        }

        json matches;
        for (int i = 0; i < mismatches + 1; i++) {
            for (const auto& sp_ep : forward_matches[i]) {
                size_t sp = std::get<0>(sp_ep);
                size_t ep = std::get<1>(sp_ep);
                for (size_t j = sp; j <= ep; j++) {
                    size_t absolute_pos = gi_forward.resolve(j);
                    coordinates pos = resolve_absolute(gi_forward.gs, absolute_pos);
                    json match = {
                        {"chr", pos.chr.name},
                        {"pos", pos.offset},
                        {"absolute_pos", absolute_pos},
                        {"strand", "+"},
                        {"distance", i}
                    };
                    matches.push_back(match);
                }
            }

            for (const auto& sp_ep : reverse_matches[i]) {
                size_t sp = std::get<0>(sp_ep);
                size_t ep = std::get<1>(sp_ep);
                for (size_t j = sp; j <= ep; j++) {
                    size_t absolute_pos = genome_length - (gi_reverse.resolve(j) + 1);
                    coordinates pos = resolve_absolute(gi_forward.gs, absolute_pos);
                    json match = {
                        {"chr", pos.chr.name},
                        {"absolute_pos", absolute_pos},
                        {"pos", pos.offset},
                        {"strand", "-"},
                        {"distance", i}
                    };
                    matches.push_back(match);
                }
            }
        }

        return matches;
    }

    /* Processes the kmers in the file, collecting all information
       about off targets and outputting it to a stream in SAM format. */
    template <class t_wt, uint32_t t_dens, uint32_t t_inv_dens>
    void process_kmers_to_stream(const genome_index<t_wt, t_dens, t_inv_dens>& gi_forward,
                                 const genome_index<t_wt, t_dens, t_inv_dens>& gi_reverse,
                                 const std::vector<std::string> &pams,
                                 size_t mismatches, int threshold,
                                 std::unique_ptr<genomics::kmer_producer>& kmer_p, std::mutex& kmer_mtx,
                                 std::ostream& output, std::mutex& output_mtx) {
        kmer out_kmer;
        while (true) {
            kmer_mtx.lock();
            bool kmers_left = kmer_p->get_next_kmer(out_kmer);
            kmer_mtx.unlock();

            if (!kmers_left) break;
            process_kmer_to_stream(gi_forward, gi_reverse, pams, mismatches, threshold, out_kmer, output, output_mtx);
        }
    }
}

#endif /* PROCESS_H */
