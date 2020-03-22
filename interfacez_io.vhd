library IEEE;
use IEEE.STD_LOGIC_1164.all;
use IEEE.numeric_std.all;
library work;
use work.zxinterfacepkg.all;
-- synthesis translate_off
use work.txt_util.all;
-- synthesis translate_on

entity interfacez_io is
  port  (
    clk_i     : in std_logic;
    rst_i     : in std_logic;

    ioreq_i   : in std_logic;
    rd_i      : in std_logic;
    wrp_i     : in std_logic;
    rdp_i     : in std_logic;
    adr_i     : in std_logic_vector(7 downto 0);
    dat_i     : in std_logic_vector(7 downto 0);
    dat_o     : out std_logic_vector(7 downto 0);
    enable_o  : out std_logic;

    border_o  : out std_logic_vector(7 downto 0);

    -- Resource FIFO connections

    signal resfifo_rd_o           : out std_logic;
    signal resfifo_read_i         : in std_logic_vector(7 downto 0);
    signal resfifo_empty_i        : in std_logic
  );

end entity interfacez_io;

architecture beh of interfacez_io is

  signal border_r : std_logic_vector(7 downto 0);
  signal addr_match_s:    std_logic;
  signal enable_s:  std_logic;
  signal dataread_r:std_logic_vector(7 downto 0);
  signal testdata_r:unsigned(7 downto 0);
begin

  border_o  <= border_r;

  addr_match_s<='1' when adr_i(0)='1' else '0';

	enable_s  <=  (not ioreq_i) and addr_match_s and not rd_i;
  enable_o  <= enable_s;

  process(clk_i, rst_i)
  begin
    if rst_i='1' then
      border_r      <= (others => '0');
      dataread_r    <= (others => 'X');
      resfifo_rd_o  <= '0';
      testdata_r    <= (others => '0');
    elsif rising_edge(clk_i) then

      resfifo_rd_o <= '0';

      if wrp_i='1' and adr_i=x"FE" then -- ULA write
        border_r <= dat_i;
        -- synthesis translate_off
        report "SET BORDER: "&hstr(dat_i);
        -- synthesis translate_on
      end if;

      if ioreq_i='0' and rdp_i='1' then
        case adr_i is
          when x"05" =>
            dataread_r <= x"39";
          when x"0B" => -- FIFO status read
            dataread_r <= "0000000" & resfifo_empty_i;
          when x"0D" => -- FIFO read
            dataread_r <= resfifo_read_i;
            -- Pop data on next clock cycle
            resfifo_rd_o <= '1';
            testdata_r <= testdata_r + 1;
          when others =>
            dataread_r <= (others => '1');
        end case;
      end if;
    end if;
  end process;

  dat_o <= dataread_r;

end beh;