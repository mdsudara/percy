#pragma once

namespace percy
{

	int 
	binomial_coeff(int n, int k)
	{
		int C[n+1][k+1];

		for (int i = 0; i <= n; i++) {
			for (int j = 0; j <= std::min(i, k); j++)
			{
				if (j == 0 || j == i) {
					C[i][j] = 1;
				} else {
					C[i][j] = C[i-1][j-1] + C[i-1][j];
				}
			}
		}

		return C[n][k];
	}

    /*******************************************************************
            Returns 1 if fanins1 > fanins2, -1 if fanins1 < fanins2, and
            0 otherwise.
    *******************************************************************/
    template<typename fanin, int FI>
    int
    colex_compare(
            const std::array<fanin, FI>& fanins1,
            const std::array<fanin, FI>& fanins2)
    {
        for (int i = FI-1; i >= 0; i--) {
            if (fanins1[i] < fanins2[i]) {
                return -1;
            } else if (fanins1[i] > fanins2[i]) {
                return 1;
            }
        }

        // All fanins are equal
        return 0;
    }

    template<typename fanin, int FI>
    int
    colex_compare(const fanin* const fanins1, const fanin* const fanins2)
    {
        for (int i = FI-1; i >= 0; i--) {
            if (fanins1[i] < fanins2[i]) {
                return -1;
            } else if (fanins1[i] > fanins2[i]) {
                return 1;
            }
        }

        // All fanins are equal
        return 0;
    }
}

