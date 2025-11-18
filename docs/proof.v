Require Import List.
Require Import Arith.
Import ListNotations.

Section DefiniteAssignmentSafety.

  (* Definitions and Hypotheses *)

  (* The line number of a variable's definition. *)
  Variable def_pos : nat.
  (* The line number of the end of the variable's lexical scope. *)
  Variable scope_end : nat.

  (* Defines what it means for a line number 'n' to be within the lexical scope
     of a variable. A line is in scope if it is between the definition and the
     end of the scope (inclusive). *)
  Definition InScope (n : nat) : Prop :=
    def_pos <= n /\ n <= scope_end.

  (* A single control flow transition, e.g., from one instruction to the next,
     or a jump. `Step u v` means control can transfer from line `u` to `v`. *)
  Variable Step : nat -> nat -> Prop.

  (* Axiom that prevents any jump from outside a variable's scope
     that bypasses its definition. *)
  Hypothesis step_safety_rule : forall u v,
    Step u v ->                (* If control flows from u to v... *)
    InScope v ->               (* and v is in the scope... *)
    v <> def_pos ->            (* and v is not the definition itself... *)
    InScope u.                 (* then u must have also been in the scope. *)

  (* Inductively defines an execution path as a list of instruction line numbers (`trace`). *)
  Inductive Path : nat -> nat -> list nat -> Prop :=
    (* A path of length 1 from a point to itself. *)
    | Path_Base : forall x, Path x x [x]
    (* If there is a path from x to y, and a step from y to z,
       then there is a path from x to z. *)
    | Path_Step : forall x y z trace,
        Path x y trace ->
        Step y z ->
        Path x z (z :: trace).

  (* The Theorem of Definite Assignment *)

  (* For any execution path from `start` to `target`:
     - If the path starts before the definition (`start < def_pos`),
     - and its target (the usage point) is in scope (`InScope target`),
     - then the definition point (`def_pos`) must be present in the path's trace. *)
  Theorem definition_dominates_use : forall start target trace,
    Path start target trace ->   (* Given a valid execution path... *)
    start < def_pos ->           (* that starts before the definition... *)
    InScope target ->            (* and ends at a use within the scope... *)
    In def_pos trace.            (* then the definition must be on that path. *)
  Proof.
    intros start target trace Hpath.

    (* The proof proceeds by induction on the structure of the execution path.
       We show that if the property holds for a path of length N, it must
       also hold for a path of length N+1. *)
    induction Hpath as [x | x y z trace_xy Hpath_xy IH_xy Hstep_yz].
    
    (* Case 1: Base case (path of length 1). *)
    (* Here, start = target = x. The hypotheses `start < def_pos` and `InScope target`
       become `x < def_pos` and `def_pos <= x`, which is a direct contradiction.
       A path of length 1 cannot satisfy the premises. *)
    - intros H_start_lt_def H_target_in_scope.
      unfold InScope in H_target_in_scope.
      destruct H_target_in_scope as [H_ge H_le].
      apply Nat.lt_nge in H_start_lt_def.
      contradiction.

    (* Case 2: Inductive step. *)
    (* Assume the theorem holds for the path to `y` (the induction hypothesis `IH_xy`)
       and prove it for the extended path to `z`. *)
    - intros H_start_lt_def H_z_in_scope.
      
      (* Analyze if the new instruction `z` is the definition point itself. *)
      destruct (Nat.eq_dec z def_pos) as [Hz_is_def | Hz_not_def].

      (* Subcase 2.1: `z` is the definition point. *)
      (* The path has reached the definition, so the goal (`In def_pos trace`)
         is trivially true. *)
      + rewrite Hz_is_def.
        apply in_eq.

      (* Subcase 2.2: `z` is not the definition point. *)
      + (* Since `z` is in scope but is not the definition, we can use the
           `step_safety_rule` to prove that the previous instruction `y`
           must also have been in scope. *)
        assert (H_y_in_scope : InScope y).
        {
          apply step_safety_rule with (v := z).
          - apply Hstep_yz.
          - apply H_z_in_scope.
          - apply Hz_not_def.
        }

        (* Apply the induction hypothesis to the shorter path ending at `y`.
           The preconditions are met: `start < def_pos` (given) and `InScope y` (just proved). *)
        assert (H_def_in_prefix : In def_pos trace_xy).
        {
          apply IH_xy.
          - assumption.      (* start < def_pos *)
          - assumption.      (* InScope y *)
        }

        (* The definition is in the prefix of the trace (`trace_xy`), so it must
           also be in the full trace (`z :: trace_xy`). *)
        apply in_cons.
        assumption.
  Qed.
End DefiniteAssignmentSafety.

Check definition_dominates_use.