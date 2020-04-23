LIBRARY ieee;
USE ieee.std_logic_1164.all;
LIBRARY work;
use work.txt_util.all;
use work.bfm_ctrlpins_p.all;

entity bfm_ctrlpins is
    port (
      Cmd_i   : in Cmd_CtrlPins_type;
      Data_o  : out Data_CtrlPins_type;

      IO26_io : inout std_logic;
      IO27_io : inout std_logic
    );
end entity bfm_ctrlpins;

architecture sim of bfm_ctrlpins is

begin

  Data_o.IO26 <= IO26_io;
  Data_o.IO27 <= IO27_io;

end sim;
