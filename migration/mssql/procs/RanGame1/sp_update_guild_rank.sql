

CREATE PROCEDURE [dbo].[sp_update_guild_rank]
    @nGuNum int,
    @nRank  int,	
	@nReturn int	OUTPUT
AS    
    DECLARE 
        @error_var int, 
		@rowcount_var int

	SET NOCOUNT ON
	
	SET @nReturn = 0	
	SET @error_var = 0
	SET @rowcount_var = 0	
    
    -- Set Guild Rank
    UPDATE GuildInfo 
    SET GuRank=@nRank 
    WHERE GuNum=@nGuNum
    
    -- Check Error
    SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT    
    IF @error_var <> 0 OR @rowcount_var = 0
    BEGIN
        SET @nReturn = -1 -- ERROR
        SET NOCOUNT OFF
        RETURN @nReturn
    END
    ELSE
    BEGIN
        SET @nReturn = 0 -- SUCCESS
        SET NOCOUNT OFF
        RETURN @nReturn
    END


