// ChipLab: ULA combinacional didatica, nao uma CPU completa.
// Resultado sem sinal. ADD e SUB descartam carry/borrow (modulo 2**WIDTH).
// op: 000=ADD, 001=SUB, 010=AND, 011=OR, 100=XOR; demais: zero.
module chiplab_alu #(
    parameter WIDTH = 8
)(
    input wire [WIDTH-1:0] a,
    input wire [WIDTH-1:0] b,
    input wire [2:0] op,
    output reg [WIDTH-1:0] result,
    output wire zero
);
    always @* begin
        case (op)
            3'b000: result = a + b;
            3'b001: result = a - b;
            3'b010: result = a & b;
            3'b011: result = a | b;
            3'b100: result = a ^ b;
            default: result = {WIDTH{1'b0}};
        endcase
    end
    assign zero = (result == {WIDTH{1'b0}});
endmodule
