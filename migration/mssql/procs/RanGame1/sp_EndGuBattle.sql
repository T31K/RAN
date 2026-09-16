
CREATE Procedure [dbo].[sp_EndGuBattle]
	@GuSNum		int,
    @GuPNum    int,
    @GuFlag int,
    @GuKillNum int,
    @GuDeathNum int,
	@nReturn int	OUTPUT	
AS
	DECLARE @error_var int, 
		    @rowcount_var int
		    
	SET NOCOUNT ON
		 
	SET @nReturn = 0	
	SET @error_var = 0
	SET @rowcount_var = 0
	
	BEGIN

IF @GuFlag = 6 --Win
begin

		UPDATE GuildInfo 
		SET GuBattleWin = GuBattleWin + 1
		WHERE GuNum=@GuSNum
End
else IF @GuFlag = 5  --Lose
Begin

		UPDATE GuildInfo 
		SET GuBattleLose = GuBattleLose + 1
		WHERE GuNum=@GuSNum
End
else IF @GuFlag = 1 --Draw
Begin

		UPDATE GuildInfo 
		SET GuBattleDraw = GuBattleDraw + 1
		WHERE GuNum=@GuPNum
End


		--INSERT INTO GuildBattle (GuSNum, GuPNum, GuFlag, GuKillNum,GuDeathNum) 
		--VALUES (@GuSNum, @GuPNum, @GuFlag, @GuKillNum,@GuDeathNum)
		
		SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
		IF @error_var <> 0 OR @rowcount_var = 0 
		BEGIN
			SET @nReturn = 0
			SET NOCOUNT OFF
			RETURN @nReturn
		END
	END
	SET @nReturn = 1
	SET NOCOUNT OFF
	RETURN @nReturn




set ANSI_NULLS ON
set QUOTED_IDENTIFIER ON

